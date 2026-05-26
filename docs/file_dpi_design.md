# File DPI Metadata Design

Design notes for writing correct horizontal and vertical DPI (PPI) metadata into BMP / PNG / JPG / TIFF files produced by BN Tech Virtual Scanner.

<details open>
<summary>中文说明</summary>

## 1. 需求

虚拟扫描仪输出（File Transfer 写出的文件，或 Native Transfer 后由应用保存的文件）必须在 Windows 资源管理器 "属性 → 详细信息" 以及第三方图像应用中显示与 settings UI 选择一致的水平分辨率和垂直分辨率。

主要需求：

- 当用户在 settings UI 选择 150 / 200 / 300 / 600 DPI 中的某一档（或应用通过 `ICAP_XRESOLUTION / ICAP_YRESOLUTION` 设定其他值）时，输出文件必须如实记录这一档 DPI。
- 必须覆盖项目支持的全部输出格式：BMP、PNG、JPG、TIFF。
- 水平和垂直分辨率可以不一致（虽然 UI 只暴露同一档，但底层 `ScannerSettings.x_resolution / y_resolution` 是独立字段），必须分别正确写入。
- 单位声明必须明确为 "每英寸" (dots/pixels per inch)，不要让消费方按 "每厘米" 或 "无单位" 解释。
- Windows Explorer "属性 → 详细信息" 中显示的水平/垂直分辨率必须与用户选择完全一致；常见消费方 XnView、IrfanView、Photoshop、NAPS2 也应一致。
- 写入失败不能破坏文件本身：要么 patch 成功并产生合法文件，要么保留 `FreeImage_Save` 的原始输出。

非功能性需求：

- DPI 修补不能改变图像像素内容，只允许修改 / 插入元数据字段或 chunk。
- DPI 修补不能依赖外部库（不能再加 libpng / libjpeg / libtiff 等），全部按字节写代码自实现，避免 FreeImage 之外多一份依赖。
- 修补过程必须对 FreeImage 已经写入的合法字段宽容（如果 FreeImage 已经写对了，覆盖一遍也应得到同样结果）。

## 2. 领域知识

### 2.1 各文件格式对 DPI 的表达方式

| 格式 | 字段 / chunk | 单位 | 数值类型 | 备注 |
|---|---|---|---|---|
| BMP | `BITMAPINFOHEADER.biXPelsPerMeter / biYPelsPerMeter` | pixels per meter | LE int32 | 直接在 DIB header 内。 |
| PNG | `pHYs` chunk | pixels per meter (unit byte = 1) | BE uint32 + 1 字节单位 | unit = 0 时单位未定义。 |
| JPG (JFIF) | APP0 (`0xFFE0`) 段的 density 字段 | density unit 1 = dpi, 2 = dpc | BE uint16 | 老 JFIF 也允许 unit = 0（无单位）。 |
| TIFF | IFD 标签 `XResolution (282)` / `YResolution (283)` + `ResolutionUnit (296)` | RATIONAL + SHORT | TIFF endianness | `ResolutionUnit` 1 = none, 2 = inch, 3 = cm。 |

### 2.2 单位换算

- DPI -> pixels per meter：`ppm = dpi / 0.0254 = dpi * 39.3700787`。
- 在 PNG / BMP 中 Windows 会把 ppm 换算回 DPI 展示，反向取整误差通常 < 1 DPI。
- JPG JFIF density 字段直接是无量纲整数，单位由 unit 字节解释，DPI 时无需换算。
- TIFF 用有理数 (numerator / denominator) 存储，本项目固定 `denominator = 100`，把 DPI 乘 100 写入 numerator，保留两位小数精度。

### 2.3 FreeImage 在 `FreeImage_Save` 后的实际行为

经实测：

- BMP：通常正确写入 `biXPelsPerMeter / biYPelsPerMeter`。
- PNG：部分 FreeImage 版本会忽略 `FreeImage_SetDotsPerMeter`，写出的 PNG 缺 `pHYs`。
- JPG：经常不写 JFIF APP0 density，或 unit 字节填 0，导致 Windows 把 DPI 当成 96。
- TIFF：通常会写 `XResolution / YResolution`，但 `ResolutionUnit` 偶尔填 1（none），使消费方无法判断单位。

因此即便已经设置了 FreeImage 内部 DPI，仍需在保存后做 "二次修补"。

### 2.4 字节序

- PNG：全部大端 (BE)。
- JPG (JFIF)：APP0 内的 density 字段大端。
- BMP：小端 (LE)，`BITMAPINFOHEADER` 字段全部小端。
- TIFF：字节序由文件头前两个字节决定，`II` = LE，`MM` = BE。

### 2.5 PNG `pHYs` chunk 结构

```text
4 bytes  Length        = 9
4 bytes  Type          = "pHYs"
4 bytes  X pixels per unit (BE uint32)
4 bytes  Y pixels per unit (BE uint32)
1 byte   Unit specifier (0 = unknown, 1 = meter)
4 bytes  CRC32 (over Type + data)
```

合法 PNG 中，`pHYs` 必须放在 `IDAT` 之前；`IHDR` 之后是最稳妥的插入点。

### 2.6 JPG JFIF APP0 段结构

```text
2 bytes  Marker        = 0xFF 0xE0
2 bytes  Length        = 16 (BE)
5 bytes  Identifier    = "JFIF\0"
2 bytes  Version       = 1.01
1 byte   Density unit  (0 = none, 1 = inch, 2 = cm)
2 bytes  X density     (BE uint16)
2 bytes  Y density     (BE uint16)
1 byte   X thumbnail   = 0
1 byte   Y thumbnail   = 0
```

如果文件已经含 APP0：原地覆盖 unit + density 字段即可。如果没有：在 `0xFFD8` (SOI) 之后插入一段全新的 APP0。

### 2.7 BMP DIB header DPI 字段位置

`BITMAPFILEHEADER` 为 14 字节，紧接着是 `BITMAPINFOHEADER`：

```text
offset 14: biSize          (4 bytes)
offset 18: biWidth         (4 bytes)
offset 22: biHeight        (4 bytes)
offset 26: biPlanes        (2 bytes)
offset 28: biBitCount      (2 bytes)
offset 30: biCompression   (4 bytes)
offset 34: biSizeImage     (4 bytes)
offset 38: biXPelsPerMeter (4 bytes LE)
offset 42: biYPelsPerMeter (4 bytes LE)
offset 46: biClrUsed       (4 bytes)
offset 50: biClrImportant  (4 bytes)
```

只需直接覆盖 offset 38 / 42 这两个 4 字节小端整数。

### 2.8 TIFF IFD 结构

文件头 8 字节给出 IFD 偏移；IFD 起始 2 字节为 entry 数，紧跟若干 12 字节 entry，每个 entry：

```text
2 bytes  Tag
2 bytes  Type    (5 = RATIONAL, 3 = SHORT, ...)
4 bytes  Count
4 bytes  Value/Offset
```

`XResolution (282)` / `YResolution (283)` 是 RATIONAL，count = 1，value/offset 字段指向文件中 8 字节 (numerator + denominator) 的位置。`ResolutionUnit (296)` 是 SHORT，count = 1，值直接放在 entry 的 value/offset 字段。

### 2.9 Windows Explorer 的 DPI 判定逻辑

- BMP：直接读 `biXPelsPerMeter / biYPelsPerMeter`，换算 DPI。
- PNG：读 `pHYs`，unit 必须为 1 才认 DPI；否则按 96 DPI 展示。
- JPG：读 JFIF APP0，unit 必须为 1 才认 DPI；某些版本会 fallback 到 EXIF `XResolution / YResolution`。
- TIFF：读 `ResolutionUnit`，必须为 2 (inch) 才认 DPI；为 1 (none) 时按像素无量纲处理。

因此 unit / `ResolutionUnit` 字段是否正确，比数值本身更关键。

## 3. 设计目标

- 覆盖项目所有输出文件格式（BMP / PNG / JPG / TIFF）。
- 不引入额外第三方依赖，全部基于 `<fstream>` + 字节级读写。
- 与 `ICAP_XRESOLUTION / ICAP_YRESOLUTION / ICAP_UNITS` 完全一致；File Transfer 与 Native Transfer 共用同一份 DPI 来源 (`ScannerSettings`)。
- 在 `FreeImage_Save` 之后总是再 patch 一次，无论 FreeImage 是否已写对：覆盖一遍等效，写错的会被纠正。
- 单元测试式自检：在 patch 前后比较像素数据长度不变，保证未误改像素。
- 单位声明使用 "per inch" 分支（PNG 经 meter 间接表达，但与 Windows 显示协议一致）。

非目标：

- 不涉及 EXIF 完整解析；只在写元数据时同时写一组 EXIF `XResolution / YResolution / ResolutionUnit` 由 FreeImage 处理。
- 不支持多页 TIFF 的多组 IFD（只处理第一个 IFD）。
- 不修补除 DPI 之外的其他元数据（color profile、orientation、color space 等不在范围内）。
- 不支持 BMP `BITMAPV4HEADER` / `BITMAPV5HEADER` 的扩展 DPI 字段（项目目前只产出 `BITMAPINFOHEADER`）。

## 4. 总体设计

```text
VirtualScanner
├── applyDpiMetadata()           // 在 FreeImage 内部写 DPI + EXIF tag
└── patchSavedDpiMetadata(fif, path)
        │ 调度到具体格式 patcher：
        ├── patchPngDpiMetadata()
        ├── patchJpegDpiMetadata()
        ├── patchBmpDpiMetadata()
        └── patchTiffDpiMetadata()
```

两层 DPI 写入：

1. 保存前调用 `applyDpiMetadata()`：
   - `FreeImage_SetDotsPerMeterX / Y` 设 FreeImage 内部字段。
   - 写一组 EXIF tag (`XResolution`, `YResolution`, `ResolutionUnit`) 到 `FIMD_EXIF_MAIN`。
   - 这一步保证 FreeImage 自己写文件时能写正确的字段。
2. 保存后调用 `patchSavedDpiMetadata(fif, path)`：
   - 按 `FREE_IMAGE_FORMAT` 派发到对应 patcher，从文件二进制层面再覆盖一次。
   - 保证即便 FreeImage 漏写或写错，最终文件里也是正确的 DPI。

公共字节工具：

- `readLittleEndian16/32`、`readBigEndian32`、`writeLittleEndian16/32`、`writeBigEndian16/32` 处理大小端。
- `crc32Png()` 实现 PNG chunk 用的 CRC32。
- `dpiToPixelsPerMeter(float dpi)`：`(unsigned)(dpi * 39.3700787 + 0.5)`。
- `dpiToJpegDensity(float dpi)`：四舍五入到 `WORD`，限制在 [1, 65535]。
- `readFileBytes(path, &buf)` / `writeFileBytes(path, buf)`：一次性整文件读写，避免半改半写。

## 5. 重要决策和原因

### 5.1 在 `FreeImage_Save` 之后总是再 patch 一次

决策：无论 FreeImage 是否已经正确写入 DPI，都强制走 patcher。

原因：

- FreeImage 不同版本/不同插件对 DPI 行为不一致（特别 PNG / JPG），靠版本检测不可靠。
- patcher 是幂等的：FreeImage 写对的字段被覆盖后值不变；FreeImage 写错的会被纠正。
- 简化分支逻辑：只有一条 "save -> patch" 路径，方便回归测试。

### 5.2 自己写字节级 patcher，不引入 libpng / libjpeg / libtiff

决策：用纯 C++ 标准库 + Windows 类型，按格式规范手写 patcher。

原因：

- DLL 体积不增大，没有额外许可证 / 链接问题。
- 只改 DPI 一处字段，专用代码比通用库更短、更可控。
- 易于在不同 MSVC / FreeImage 版本之间保持一致行为。

### 5.3 每个格式用 unit = "inch"

决策：

- PNG：unit = 1（meter），数值用 `dpi * 39.37` 间接表达 DPI（这是 PNG 规范的等效做法）。
- JPG：density unit = 1（dots per inch），density 字段直接用 DPI。
- BMP：`biXPelsPerMeter / biYPelsPerMeter` 用 `dpi * 39.37`（DIB 本身没有 unit 概念，全靠 ppm）。
- TIFF：`ResolutionUnit = 2`（inch），`XResolution / YResolution` 用 `DPI * 100 / 100` 的有理数。

原因：

- Windows Explorer 与主流图像应用都把 PNG `pHYs` unit=1 / JPG JFIF unit=1 / TIFF `ResolutionUnit=2` 视为 DPI。
- 与 TWAIN 项目 `ICAP_UNITS = TWUN_INCHES` 一致。
- 避免 unit = 0 这类 "未知单位" 让消费方退化到默认 96 DPI 显示。

### 5.4 BMP 直接定位 offset 38 / 42 写两个 LE int32

决策：BMP patcher 不解析整个 DIB header，只检查 magic + DIB header 大小 >= 40，然后定位到固定 offset 写入。

原因：

- 项目产出的 BMP 都使用 `BITMAPINFOHEADER`（40 字节），不会出现 `BITMAPV4HEADER` 等扩展头。
- DIB header 起始位置和 DPI 字段偏移是规范保证的（offset 38 / 42 相对文件起点）。
- 简单代码、最低读改写成本。

### 5.5 PNG 在 `IHDR` 之后插入 `pHYs`

决策：如果文件已经有 `pHYs`，原地覆盖；否则在 `IHDR` chunk 末尾后插入新 `pHYs`。

原因：

- PNG 规范要求 `pHYs` 必须出现在第一个 `IDAT` 之前，而 `IHDR` 是文件第一个 chunk，紧跟其后的位置最稳妥。
- 不破坏其他元数据 chunk（`gAMA`, `cHRM`, `iCCP`, `tEXt` 等）。
- 自实现 CRC32 (`crc32Png`)，保证插入的 chunk 在所有 PNG 解码器里都合法。

### 5.6 JPG 优先覆盖现有 APP0，没有就在 SOI 后插入

决策：扫描所有 marker 段：

- 找到 JFIF APP0 (`0xFFE0`，identifier "JFIF\0") 则原地把 unit + density 字段写成 DPI。
- 遇到 `0xFFD9` (EOI) / `0xFFDA` (SOS) 之前没找到 JFIF APP0，则在 `0xFFD8` (SOI) 之后插入一段 18 字节的 JFIF APP0。

原因：

- 大多数 FreeImage 写出的 JPG 已经有 JFIF APP0，但 density 字段经常错；覆盖比插入便宜。
- 没有 APP0 时，必须在 SOI 后立刻插入，不能放到 SOS 之后；放错位置会让一些解码器报告损坏。
- 18 字节的标准 JFIF 段足以满足 Windows / Explorer / Photoshop 的解析。

### 5.7 TIFF 只处理第一个 IFD，只覆盖现有 entry

决策：TIFF patcher 只解析第 0 个 IFD 的 entry 列表，找到 282 / 283 / 296 时原地覆盖，不新增 entry，不重排 IFD。

原因：

- 项目输出的 TIFF 都是单页，且 FreeImage 在 IFD 中至少会写出 `XResolution / YResolution / ResolutionUnit` 三项 entry。
- 重排 IFD 会牵动 entry count、value/offset 链、可能的多 IFD `next IFD` 指针，复杂度远超收益。
- 一旦发现关键 entry 缺失（极少见），patcher 直接返回 `false`，保留原文件不变；上游可以选择重写或忽略。

### 5.8 `applyDpiMetadata` 兼写 EXIF tag

决策：`applyDpiMetadata` 不仅设 FreeImage 内部 ppm，还创建 EXIF `XResolution / YResolution / ResolutionUnit` 标签。

原因：

- 部分图像应用读 EXIF 而不读容器格式自己的 DPI 字段。
- 让 FreeImage 在写文件时同时把 EXIF 一起写出去（TIFF、JPG）。
- 即便 patcher 失败（如文件被瞬时锁），EXIF tag 也能提供一份退路。

## 6. 架构各组件改动点

### 6.1 `virtual_scanner.h`

- 新增私有方法声明：
  - `void applyDpiMetadata();`
  - `void patchSavedDpiMetadata(FREE_IMAGE_FORMAT fif, const std::string& path);`

### 6.2 `virtual_scanner.cpp`（内部匿名 namespace）

新增字节级辅助：

- `readLittleEndian16/32`、`readBigEndian32`：从 `std::vector<BYTE>` 偏移读取。
- `writeLittleEndian16/32`、`writeBigEndian16/32`：写入。
- `crc32Png(const BYTE*, size_t)`：PNG chunk 用 CRC32（位反向多项式 `0xEDB88320`）。
- `dpiToPixelsPerMeter(float dpi)`：浮点 -> ppm 四舍五入。
- `dpiToJpegDensity(float dpi)`：浮点 -> JFIF density 四舍五入。
- `readFileBytes / writeFileBytes`：整文件二进制 I/O。
- `makePngPhysChunk / makeJpegJfifApp0Segment`：构造标准 chunk / 段。

新增四个 patcher：

- `patchPngDpiMetadata(path, x_dpi, y_dpi)`：扫描 PNG chunk，找到 `pHYs` 就原地替换，否则在 `IHDR` 之后插入。
- `patchJpegDpiMetadata(path, x_dpi, y_dpi)`：扫描 JPG marker，找到 JFIF APP0 就覆盖 density，否则在 SOI 后插入新 APP0。
- `patchBmpDpiMetadata(path, x_dpi, y_dpi)`：定位到固定 offset 38 / 42 写 `biXPelsPerMeter / biYPelsPerMeter`。
- `patchTiffDpiMetadata(path, x_dpi, y_dpi)`：解析头 + 第一个 IFD，覆盖 tag 282 / 283 的 RATIONAL value 和 tag 296 的 SHORT。

### 6.3 `VirtualScanner::applyDpiMetadata()`

- 用 `FreeImage_SetDotsPerMeterX / Y` 设置 FreeImage 内部 DPI。
- 创建 `XResolution (0x011A)` / `YResolution (0x011B)` (`FIDT_RATIONAL`)，分母固定 100。
- 创建 `ResolutionUnit (0x0128)` (`FIDT_SHORT`，值 2 = inch)。
- 通过 `FreeImage_SetMetadata(FIMD_EXIF_MAIN, dib_, key, tag)` 挂到当前 DIB。

### 6.4 `VirtualScanner::patchSavedDpiMetadata()`

- 从 `settings_.x_resolution / y_resolution` 取 DPI（缺省 300 DPI）。
- 根据 `fif` 派发：
  - `FIF_PNG` -> `patchPngDpiMetadata`
  - `FIF_JPEG` -> `patchJpegDpiMetadata`
  - `FIF_BMP` -> `patchBmpDpiMetadata`
  - `FIF_TIFF` -> `patchTiffDpiMetadata`

### 6.5 `VirtualScanner::saveImageToFile()` / `saveImageToPath()`

- 在 `FreeImage_Save(...)` 之前调用 `applyDpiMetadata()`，保证内部字段最新。
- 在保存成功 (`saved == TRUE`) 之后调用 `patchSavedDpiMetadata(fif, path)`，按文件类型覆盖容器级 DPI。
- patcher 失败不视为整体失败：文件仍是合法图像，只是 DPI 可能回落到 FreeImage 输出值。

### 6.6 `twain_data_source.cpp`

- File Transfer / Native Transfer 都从同一份 `ScannerSettings` 取 DPI，本设计不需要在 DS 层再次干预。
- DS 层只确保 `getImageInfo()` 返回的 `TW_IMAGEINFO.XResolution / YResolution` 与 `ScannerSettings` 同源，且 `allocAndFillDibHeader()` 把同样的 DPI 写入 `biXPelsPerMeter / biYPelsPerMeter`，使 Native Transfer 后应用保存出来的 DIB / BMP 也带正确 DPI。

## 7. 典型流程示例

### 7.1 File Transfer 保存 PNG

```text
1. settings_.x_resolution = 600, settings_.y_resolution = 600
2. FreeImage 出图后 saveImageToFile():
     applyDpiMetadata()
       -> FreeImage_SetDotsPerMeter(dib_, 23622, 23622)  // 600 * 39.37
       -> EXIF XResolution = 60000/100, YResolution = 60000/100, ResUnit = 2
     FreeImage_Save(FIF_PNG, dib_, "D:\\scans\\a.png", 0)
     patchSavedDpiMetadata(FIF_PNG, "D:\\scans\\a.png")
       -> patchPngDpiMetadata(...)
            读全文件 -> 扫描 chunk
            发现 pHYs -> 替换为新 chunk
            ppm = 23622, unit = 1
            写回文件
3. Windows Explorer 属性 -> 详细信息：
     水平分辨率 600 dpi，垂直分辨率 600 dpi
```

### 7.2 File Transfer 保存 JPG（FreeImage 漏写 JFIF density）

```text
1. settings_.x_resolution = 300, settings_.y_resolution = 300
2. FreeImage_Save 出的 JPG，APP0 density 单位为 0 (none)
3. patchJpegDpiMetadata("D:\\scans\\a.jpg", 300, 300)
   -> 在 marker 序列里发现 JFIF APP0
   -> 覆盖：unit = 1, x_density = 300, y_density = 300
   -> 写回
4. Windows Explorer：300 dpi / 300 dpi
```

### 7.3 Native Transfer 后应用保存 BMP

```text
1. allocAndFillDibHeader() 写入 biXPelsPerMeter = biYPelsPerMeter = round(150 * 39.37)
2. 应用 GlobalLock 拿到 DIB，自己写 BMP 文件，DIB header 原样写出
3. Explorer 显示 150 dpi / 150 dpi（无需 patcher，因为 DIB header 本身已正确）
```

## 8. 限制

- 修补失败时返回 `false`，但调用方目前不会因此 retry 或回退；文件仍然存在，DPI 可能不准。
- BMP patcher 仅支持 `BITMAPINFOHEADER`；若将来生成 `BITMAPV4HEADER` 或 `BITMAPV5HEADER`，offset 38 / 42 的语义会变。
- TIFF patcher 仅处理第一个 IFD，多页 TIFF 的后续页 DPI 不会被覆盖（目前项目不产出多页 TIFF）。
- TIFF patcher 不会新增 entry：若 FreeImage 没写 `XResolution / YResolution / ResolutionUnit`（极少见），patcher 直接返回 false。
- TIFF 分子写死乘以 100：极端高 DPI（>4.2 亿 / 100）会溢出 uint32，但实际不可能。
- PNG patcher 在文件没有 `IHDR` 时返回 false；FreeImage 写出的 PNG 一定有 `IHDR`，但损坏文件会被原样保留。
- JPG patcher 不处理无 SOI 的退化文件、不处理 RST marker 之外的奇怪 marker 序列。
- patcher 用 `std::ifstream / ofstream` 整文件读写，大文件内存占用与文件大小线性相关。
- 单位永远写成 "per inch"，没有暴露给应用 / UI 选择厘米单位的入口。
- EXIF tag 只挂在 `FIMD_EXIF_MAIN`，没有写 EXIF IFD0 的对等字段；某些只看 IFD0 的应用可能仍读不到。

## 9. 下一步工作

- 给四个 patcher 增加单元测试：固定输入字节序列 + DPI，比较 patch 后输出。
- 在 patcher 失败时记录日志（`OutputDebugStringA`），并在 UI 上提示 "DPI 元数据写入失败"。
- 支持 BMP V4 / V5 header（如果将来想用更宽的 color profile，需要重新定位 DPI 字段）。
- 支持多页 TIFF：遍历 `next IFD` 指针，对每个 IFD 做同样的覆盖。
- 给 TIFF / JPG 增加 "缺失则插入" 的能力，覆盖 FreeImage 完全没写 entry / APP0 的情况。
- 把 DPI 单位选择暴露到 settings UI / capability 协商（添加 `TWUN_CENTIMETERS` 支持，需要同时调整 patcher 单位字段）。
- 在 `applyDpiMetadata` 内同时写 EXIF IFD0 等价字段，扩大兼容范围。
- 引入文件原子写：先写 `.tmp` 再 rename，避免 patch 中途崩溃留下损坏的图像文件。
- 加入跨格式 DPI 一致性自检：保存后立即重新读取，比较 `FreeImage_GetDotsPerMeter` 是否与设定一致，作为 CI 烟雾测试。
- 评估 PDF 输出场景（未来需求），PDF 用 `/UserUnit` 或图像 stream 的 `Width / Height + Decode` 表达 DPI，与现有 patcher 思路差异较大。

</details>

<details>
<summary>English</summary>

## 1. Requirement

Files produced by the virtual scanner (whether written by File Transfer or saved by the application after Native Transfer) must show the correct horizontal and vertical DPI in Windows Explorer's Details tab and in third-party image applications, matching the value selected in the settings UI.

Main requirements:

- When the user picks 150 / 200 / 300 / 600 DPI in the settings UI (or the application sets a different value via `ICAP_XRESOLUTION / ICAP_YRESOLUTION`), the output file must record exactly that DPI.
- Cover every supported output format: BMP, PNG, JPG, TIFF.
- Horizontal and vertical DPI must be written independently (the UI exposes a single value, but `ScannerSettings.x_resolution / y_resolution` are independent fields).
- The unit declaration must be unambiguously "per inch"; consumers must not interpret the values as cm or unitless.
- Windows Explorer's Details tab must show exactly the selected DPI; common consumers (XnView, IrfanView, Photoshop, NAPS2) must agree.
- Failures must not corrupt the file: either the patch succeeds and produces a valid file, or the original `FreeImage_Save` output is preserved.

Non-functional:

- DPI patching must not alter pixel data; only metadata fields or chunks.
- No additional dependencies (no libpng / libjpeg / libtiff); handcraft byte-level writers.
- Patching must be tolerant of fields FreeImage already wrote correctly; a re-patch over correct values must yield identical output.

## 2. Domain knowledge

### 2.1 How each format expresses DPI

| Format | Field / chunk | Unit | Type | Notes |
|---|---|---|---|---|
| BMP | `BITMAPINFOHEADER.biXPelsPerMeter / biYPelsPerMeter` | pixels per meter | LE int32 | Inside the DIB header. |
| PNG | `pHYs` chunk | pixels per meter (unit byte = 1) | BE uint32 + 1-byte unit | Unit = 0 means undefined. |
| JPG (JFIF) | APP0 (`0xFFE0`) density fields | unit 1 = dpi, 2 = dpcm | BE uint16 | Older JFIF allows unit = 0. |
| TIFF | IFD tags `XResolution (282)` / `YResolution (283)` + `ResolutionUnit (296)` | RATIONAL + SHORT | TIFF endianness | `ResolutionUnit` 1 = none, 2 = inch, 3 = cm. |

### 2.2 Unit conversion

- DPI -> pixels per meter: `ppm = dpi * 39.3700787`.
- Windows converts ppm back to DPI for display; round-trip error is typically < 1 DPI.
- JFIF density values are bare integers; the unit byte selects DPI vs DPC.
- TIFF stores rationals (`numerator / denominator`); the project fixes `denominator = 100`, giving two-decimal precision.

### 2.3 Actual FreeImage behavior

- BMP: usually correct.
- PNG: some FreeImage versions ignore `FreeImage_SetDotsPerMeter` and emit no `pHYs`.
- JPG: JFIF density frequently missing or unit = 0, making Windows fall back to 96 DPI.
- TIFF: `XResolution / YResolution` usually present, but `ResolutionUnit` sometimes 1 (none).

A second-pass patch is therefore needed.

### 2.4 Endianness

- PNG: big-endian.
- JPG JFIF: big-endian for density fields.
- BMP: little-endian.
- TIFF: byte order set by `II` (LE) or `MM` (BE) in the header.

### 2.5 PNG `pHYs` chunk layout

```text
4 bytes  Length        = 9
4 bytes  Type          = "pHYs"
4 bytes  X pixels per unit (BE)
4 bytes  Y pixels per unit (BE)
1 byte   Unit          (0 = unknown, 1 = meter)
4 bytes  CRC32 (over Type + data)
```

`pHYs` must appear before the first `IDAT`; placing it right after `IHDR` is safest.

### 2.6 JPG JFIF APP0 layout

```text
2 bytes  Marker     = 0xFF 0xE0
2 bytes  Length     = 16 (BE)
5 bytes  Identifier = "JFIF\0"
2 bytes  Version    = 1.01
1 byte   Unit       (0 = none, 1 = inch, 2 = cm)
2 bytes  X density  (BE)
2 bytes  Y density  (BE)
1 byte   X thumbnail = 0
1 byte   Y thumbnail = 0
```

Overwrite the existing APP0 if present; otherwise insert a fresh one immediately after SOI (`0xFFD8`).

### 2.7 BMP DPI offsets

`biXPelsPerMeter` is at file offset 38, `biYPelsPerMeter` at 42 (assuming `BITMAPFILEHEADER` + `BITMAPINFOHEADER`).

### 2.8 TIFF IFD entry

```text
2 bytes  Tag
2 bytes  Type    (5 = RATIONAL, 3 = SHORT)
4 bytes  Count
4 bytes  Value/Offset
```

`XResolution (282)` / `YResolution (283)` are RATIONAL with count = 1; the value/offset points to 8 bytes (numerator + denominator). `ResolutionUnit (296)` is SHORT with count = 1, stored inline.

### 2.9 Windows Explorer interpretation

Explorer needs the right unit byte to display DPI:

- PNG / JFIF: unit must be 1; otherwise it shows 96 DPI.
- TIFF: `ResolutionUnit` must be 2 (inch).
- BMP: always reads ppm and computes DPI.

The unit declaration is more important than the numeric value.

## 3. Design goals

- Cover BMP / PNG / JPG / TIFF.
- No additional third-party dependencies; pure `<fstream>` byte I/O.
- DPI source is shared with `ICAP_XRESOLUTION / ICAP_YRESOLUTION / ICAP_UNITS` and with File / Native Transfer (single `ScannerSettings`).
- Always re-patch after `FreeImage_Save`, regardless of FreeImage's success.
- Use the "per inch" unit branch in every format.

Non-goals:

- No full EXIF parser; just write one set of EXIF tags via FreeImage.
- No multi-page TIFF IFD chains.
- No metadata other than DPI (color profile, orientation, color space remain out of scope).
- No BMP V4 / V5 headers (the project only emits `BITMAPINFOHEADER`).

## 4. Overall design

```text
VirtualScanner
├── applyDpiMetadata()           // Sets FreeImage internal DPI + EXIF tags
└── patchSavedDpiMetadata(fif, path)
        ├── patchPngDpiMetadata()
        ├── patchJpegDpiMetadata()
        ├── patchBmpDpiMetadata()
        └── patchTiffDpiMetadata()
```

Two-layer DPI writing:

1. Before save: `applyDpiMetadata()` sets `FreeImage_SetDotsPerMeterX / Y` and writes EXIF `XResolution / YResolution / ResolutionUnit` so FreeImage outputs the right fields where it can.
2. After save: `patchSavedDpiMetadata(fif, path)` re-writes the container-level fields by byte-level patching, guaranteeing the final file is correct even if FreeImage missed them.

Shared byte utilities: little / big-endian readers and writers, PNG `crc32Png`, `dpiToPixelsPerMeter`, `dpiToJpegDensity`, whole-file `readFileBytes` / `writeFileBytes`, and `makePngPhysChunk` / `makeJpegJfifApp0Segment` constructors.

## 5. Key decisions and rationale

### 5.1 Always re-patch after `FreeImage_Save`

Patchers are idempotent: correct fields stay correct, wrong fields get fixed. Keeps the code path simple and avoids version-detection.

### 5.2 Hand-written byte-level patchers, no extra libs

No new dependencies, no licensing concerns, no DLL bloat. Specialized code for a single field is easier to maintain than pulling in libpng / libjpeg / libtiff just for DPI.

### 5.3 "Per inch" unit in every format

PNG ppm is mathematically equivalent to DPI (`ppm = dpi * 39.37`); JFIF and TIFF have explicit inch units. Setting them consistently matches Windows Explorer's DPI display logic and aligns with `ICAP_UNITS = TWUN_INCHES`.

### 5.4 BMP: write directly to offsets 38 / 42

The project only emits `BITMAPINFOHEADER`, so the offsets are fixed. Avoids parsing the DIB header.

### 5.5 PNG: replace existing `pHYs` or insert after `IHDR`

`pHYs` must precede `IDAT`; right after `IHDR` is always legal and easy to locate. Self-implemented CRC32 ensures any PNG decoder accepts the chunk.

### 5.6 JPG: overwrite JFIF APP0 if present, otherwise insert after SOI

Most FreeImage JPGs already carry JFIF APP0 (just with the wrong unit/density). When missing, inserting a 18-byte standard APP0 after `0xFFD8` is the safest position.

### 5.7 TIFF: first IFD only, overwrite existing entries

The project produces single-page TIFFs. FreeImage reliably writes the three entries; the patcher only needs to overwrite them. Adding entries would require shifting the IFD and patching value/offset pointers, which is far more invasive.

### 5.8 `applyDpiMetadata` also writes EXIF tags

Some consumers read EXIF rather than container-specific fields. Writing EXIF gives a fallback even if container-level patching fails.

## 6. Component changes

### 6.1 `virtual_scanner.h`

New private methods:

- `void applyDpiMetadata();`
- `void patchSavedDpiMetadata(FREE_IMAGE_FORMAT fif, const std::string& path);`

### 6.2 `virtual_scanner.cpp` (anonymous namespace)

Byte helpers (`readLittleEndian16/32`, `readBigEndian32`, `writeLittleEndian16/32`, `writeBigEndian16/32`), `crc32Png`, `dpiToPixelsPerMeter`, `dpiToJpegDensity`, `readFileBytes`, `writeFileBytes`, `makePngPhysChunk`, `makeJpegJfifApp0Segment`.

Four patchers:

- `patchPngDpiMetadata`: scan chunks, replace `pHYs` if present, otherwise insert after `IHDR`.
- `patchJpegDpiMetadata`: scan markers, overwrite JFIF APP0 density + unit, otherwise insert APP0 after SOI.
- `patchBmpDpiMetadata`: validate magic and DIB header size, then write LE int32s at offsets 38 / 42.
- `patchTiffDpiMetadata`: parse header + first IFD, overwrite tags 282 / 283 / 296 in place.

### 6.3 `VirtualScanner::applyDpiMetadata()`

- `FreeImage_SetDotsPerMeterX / Y` for the FreeImage internal fields.
- Create EXIF tags: `XResolution (0x011A)` and `YResolution (0x011B)` (`FIDT_RATIONAL`, denominator 100), `ResolutionUnit (0x0128)` (`FIDT_SHORT`, value 2).
- Attach via `FreeImage_SetMetadata(FIMD_EXIF_MAIN, dib_, key, tag)`.

### 6.4 `VirtualScanner::patchSavedDpiMetadata()`

- Read DPI from `settings_.x_resolution / y_resolution` (fallback 300).
- Dispatch on `FREE_IMAGE_FORMAT` to the matching patcher.

### 6.5 `VirtualScanner::saveImageToFile()` / `saveImageToPath()`

- Call `applyDpiMetadata()` before `FreeImage_Save`.
- Call `patchSavedDpiMetadata()` after a successful save.
- Patcher failure does not fail the overall save; the file remains valid.

### 6.6 `twain_data_source.cpp`

- File / Native Transfer share `ScannerSettings`, so the DS layer needs no additional DPI logic.
- `getImageInfo()` returns `TW_IMAGEINFO.XResolution / YResolution` from the same `ScannerSettings`; `allocAndFillDibHeader()` writes the same DPI into `biXPelsPerMeter / biYPelsPerMeter`, so Native-Transfer DIBs saved by the application also carry the correct DPI.

## 7. Typical flows

### 7.1 File Transfer saves a PNG

```text
1. settings_.x_resolution = 600, y_resolution = 600
2. saveImageToFile():
     applyDpiMetadata() -> FreeImage_SetDotsPerMeter(dib_, 23622, 23622)
                       -> EXIF XResolution = 60000/100, etc.
     FreeImage_Save(FIF_PNG, dib_, "D:\\scans\\a.png", 0)
     patchSavedDpiMetadata(FIF_PNG, "D:\\scans\\a.png")
       -> patchPngDpiMetadata: replace existing pHYs with ppm=23622, unit=1
3. Explorer: 600 dpi / 600 dpi.
```

### 7.2 File Transfer saves a JPG (FreeImage misses JFIF density)

```text
1. settings_.x_resolution = 300, y_resolution = 300
2. FreeImage's JPG has APP0 unit = 0
3. patchJpegDpiMetadata("D:\\scans\\a.jpg", 300, 300)
   -> finds APP0 -> writes unit = 1, x = y = 300
4. Explorer: 300 dpi / 300 dpi.
```

### 7.3 Native Transfer, application saves a BMP

```text
1. allocAndFillDibHeader writes biXPelsPerMeter = biYPelsPerMeter = round(150 * 39.37)
2. App locks the DIB, writes the BMP itself, DIB header is preserved
3. Explorer: 150 dpi / 150 dpi (no patcher needed; DIB already correct).
```

## 8. Limitations

- Patcher failure returns `false` but is not retried; the saved file remains, possibly with wrong DPI.
- BMP patcher assumes `BITMAPINFOHEADER`; would need updates for V4 / V5 headers.
- TIFF patcher handles only the first IFD; multi-page TIFFs would need IFD chain traversal.
- TIFF patcher only overwrites existing entries; missing entries cause it to skip.
- TIFF rational numerator multiplies by 100; extreme high DPI could overflow `uint32`.
- PNG patcher requires `IHDR` to be present; malformed files are left untouched.
- JPG patcher does not handle corrupt files lacking SOI or with unexpected marker sequences.
- File I/O is whole-file in memory; very large files consume proportional memory.
- Unit is hard-coded to "per inch"; there is no UI or capability to switch to centimeters.
- EXIF tags are written only to `FIMD_EXIF_MAIN`; consumers that read EXIF IFD0 might miss them.

## 9. Next steps

- Add unit tests for the four patchers (fixed input bytes + DPI, byte-compare output).
- Log patcher failures (`OutputDebugStringA`) and surface a warning in the settings UI on failure.
- Support BMP V4 / V5 headers if the project ever needs ICC profile output.
- Multi-page TIFF support: traverse `next IFD` and patch each IFD's entries.
- Insert TIFF entries / JPG APP0 when they are entirely missing.
- Add `TWUN_CENTIMETERS` support end-to-end (capability + patcher unit fields).
- Also write equivalent EXIF IFD0 fields for wider tool compatibility.
- Atomic file writes (`.tmp` + rename) so a crash mid-patch never corrupts the saved file.
- Add a post-save round-trip check: re-read DPI and compare against the setting as a smoke test.
- Evaluate future PDF output: PDF expresses DPI through `/UserUnit` or the image stream's `Width / Height + Decode`, which would require a different patcher strategy.

</details>
