# Pixel Type Design (Color / Gray / BW)

Design notes for delivering Color (24-bit RGB), Gray (8-bit grayscale), and BW (1-bit bitonal) images consistently across Native Transfer and File Transfer in BN Tech Virtual Scanner.

<details open>
<summary>中文说明</summary>

## 1. 需求

虚拟扫描仪必须按用户/应用选择的像素类型输出图像，三种像素类型在 TWAIN 中对应：

- `TWPT_RGB` — 24-bit 真彩色（每像素 R / G / B 各 8-bit）
- `TWPT_GRAY` — 8-bit 灰度（每像素 0..255）
- `TWPT_BW`  — 1-bit 黑白（每像素 0/1，1 表示纸面 chocolate flavor 中的"白纸"或 vanilla flavor 中的"墨迹"）

主要功能需求：

- TWAIN 应用通过 `ICAP_PIXELTYPE` 设置三者之一时，DS 必须按目标格式真实地转换图像（而不是只声明类型却返回 24-bit）。
- settings UI 提供 Color / Gray / BW 三选一，行为与 caps 设置等价。
- Native Transfer 返回的 DIB（`BITMAPINFOHEADER` + palette + rows）必须用正确的 `biBitCount`：
  - RGB → 24
  - GRAY → 8（含 256 项灰度调色板）
  - BW → 1（含 2 项黑白调色板）
- File Transfer 写出的 PNG / JPG / BMP / TIFF 文件像素位深必须匹配像素类型（JPEG 不支持 1-bit，需要 fallback 到 8-bit 或拒绝）。
- 像素类型转换必须发生在按 DPI 重采样之后，避免黑白量化被插值打回灰度。
- 像素类型 + DPI + page size 三者必须正交：任意组合都能正常出图，TW_IMAGEINFO 上报字段一致。
- 与 `ICAP_PIXELFLAVOR = TWPF_CHOCOLATE` 保持一致：0 = 黑、1 = 白（"巧克力"语义）。

非功能性需求：

- 转换必须是无损（在数学允许的范围内）的：BW 用阈值化、GRAY 用感知亮度加权、RGB 直通。
- 不引入除 FreeImage 之外的额外依赖。
- 一次扫描只做一次像素类型转换。
- 输出文件能在 Windows 图片查看器、XnView、Photoshop、NAPS2 中正确显示。

## 2. 领域知识

### 2.1 TWAIN 像素类型

`ICAP_PIXELTYPE` 是 `TW_UINT16`，常见值：

| 值 | 名称 | 含义 |
|---|---|---|
| 0 | `TWPT_BW`   | 1-bit 黑白 |
| 1 | `TWPT_GRAY` | 8-bit 灰度 |
| 2 | `TWPT_RGB`  | 24-bit 真彩色 |

本项目支持上面三种，注册在 `ICAP_PIXELTYPE` 的 ENUMERATION 容器中，默认 `TWPT_RGB`。

`ICAP_BITDEPTH` 是另一相关能力，与 `ICAP_PIXELTYPE` 联动：

- RGB → 24
- GRAY → 8
- BW → 1

本项目按 PIXELTYPE 推导 BITDEPTH，不单独让应用设置 BITDEPTH。

`ICAP_PIXELFLAVOR` 决定 0 / 1 在 BW（以及 8-bit 灰度的"黑端"）代表什么：

- `TWPF_CHOCOLATE` = 0 表示黑、1 表示白（最常见、本项目默认）
- `TWPF_VANILLA`   = 0 表示白、1 表示黑

### 2.2 DIB 的位深与调色板规则

Windows DIB (`BITMAPINFOHEADER` + 像素行) 不同位深的硬性要求：

| biBitCount | 调色板 | 行字节排列 |
|---|---|---|
| 24 | 无 | BGR BGR BGR ...，行 4 字节对齐 |
| 8  | 必须 256 项 RGBQUAD | 每字节一个调色板索引 |
| 1  | 必须 2 项 RGBQUAD | 每位一个索引，MSB 优先；行 4 字节对齐 |

- 8-bit 灰度 DIB 的调色板必须填 256 项 (i,i,i,0)。
- 1-bit DIB 的调色板必须填 2 项：index 0 = 黑 (0,0,0,0)，index 1 = 白 (255,255,255,0)。
- 行宽计算：`((biWidth * biBitCount + 31) / 32) * 4`。

如果 DIB 头声明 8-bit 却不带调色板，Windows 图像 API 会渲染异常或拒绝。

### 2.3 FreeImage 中三种像素类型

FreeImage 内部 FIBITMAP 的位深与本项目像素类型映射：

| TWAIN | FreeImage 位深 | 接口 |
|---|---|---|
| `TWPT_RGB`  | 24 (`FIT_BITMAP` + 24 bpp, BGR) | `FreeImage_ConvertTo24Bits` |
| `TWPT_GRAY` | 8  (`FIT_BITMAP` + 8 bpp + 256 灰阶调色板) | `FreeImage_ConvertTo8Bits` 或 `FreeImage_ConvertToGreyscale` |
| `TWPT_BW`   | 1  (`FIT_BITMAP` + 1 bpp + 2 项调色板) | `FreeImage_Threshold` 或 `FreeImage_Dither` |

注意：

- `FreeImage_ConvertTo8Bits` 会按 Windows 半色调调色板转换，不一定是灰度；要拿真正的 8-bit 灰度图必须用 `FreeImage_ConvertToGreyscale`。
- `FreeImage_Threshold(src, T)` 把灰度二值化，T 默认建议 128；阈值大表示更多像素被判为黑（chocolate）。
- `FreeImage_Dither(src, FID_FS)` 用 Floyd-Steinberg 抖动产生 1-bit 图，适合照片；纯文档建议用阈值。

### 2.4 灰度的感知亮度公式

把 RGB → GRAY 最常用的两套权重：

- BT.601：`Y = 0.299 R + 0.587 G + 0.114 B`
- BT.709：`Y = 0.2126 R + 0.7152 G + 0.0722 B`

FreeImage `FreeImage_ConvertToGreyscale` 内部用 BT.601。对扫描类内容差别极小，本项目沿用 FreeImage 的默认。

### 2.5 文件格式对像素类型的支持差异

| 格式 | 1-bit | 8-bit Gray | 24-bit RGB |
|---|---|---|---|
| PNG  | ✅ | ✅ | ✅ |
| TIFF | ✅ (G4 / LZW / Raw) | ✅ | ✅ |
| BMP  | ✅ | ✅ | ✅ |
| JPEG | ❌ | ✅ | ✅ |

JPEG 没有 1-bit 模式，遇到 BW 输出 JPEG 必须做选择：

- A. 自动 fallback 到 8-bit 灰度后再编码（应用拿到的还是 JPG，但是灰度，二者像素并不一致）。
- B. 拒绝该组合，返回 `TWCC_BADCAP`。

本项目选 A（fallback）：保证应用不会扫描失败；但 `TW_IMAGEINFO.PixelType` 仍按用户请求上报 `TWPT_BW`，避免破坏协议契约（文件像素与协议字段允许有差异，应用一般以协议为准）。

### 2.6 像素类型与 DPI 重采样的顺序

见 `implement_dpi_design.md` §2.3：必须先在 24-bit BGR 上做 `FreeImage_Rescale`，再按目标像素类型转换。否则：

- 先二值化再缩放：缩放后出现 0..255 灰度像素，违反 1-bit 语义。
- 先灰度化再缩放：可行，但灰度阈值后处理（如自适应阈值）效果差。

## 3. 设计

### 3.1 数据流

```
[App / UI sets ICAP_PIXELTYPE]
        │
        ▼
TwainDataSource::handleDatCapability  (MSG_SET pixel_type)
        │  写入 caps_
        ▼
TwainDataSource::updateScannerFromCaps()
        │  ScannerSettings.pixel_type = caps_[ICAP_PIXELTYPE]
        ▼
VirtualScanner::preScanPrep(ScannerSettings)
        │  acquireImage → ensure24BitDib
        │  applyPageSizeScaling (在 24-bit BGR 上完成)
        │  applyPixelFormat(s.pixel_type) ── 切换到目标位深
        │  applyDpiMetadata
        ▼
current_fibitmap_  (1 / 8 / 24 bpp)
        │
        ├──► Native: getDibImage → BITMAPINFOHEADER(biBitCount)
        │                          + palette (1-bit / 8-bit)
        │                          + 行数据 (4 字节对齐)
        │
        └──► File: saveImageToFile → 按格式选择编码 / fallback
```

### 3.2 ScannerSettings.pixel_type

```cpp
enum class PixelType : uint16_t {
  BW   = TWPT_BW,
  Gray = TWPT_GRAY,
  RGB  = TWPT_RGB,
};
```

`updateScannerFromCaps()` 直接把 caps 容器的当前 ONEVALUE 写入 `settings_.pixel_type`。

### 3.3 capability 层

`capability.cpp` 注册：

```cpp
addCap(ICAP_PIXELTYPE, TWTY_UINT16, TWON_ENUMERATION,
       TWPT_RGB,                       // default
       {TWPT_BW, TWPT_GRAY, TWPT_RGB}); // values
```

`MSG_SET` 时校验值在集合内；不在则 `TWCC_BADVALUE`。

同时注册：

```cpp
addCap(ICAP_PIXELFLAVOR, TWTY_UINT16, TWON_ONEVALUE,
       TWPF_CHOCOLATE, {TWPF_CHOCOLATE});
```

`ICAP_BITDEPTH` 在 GET / GETCURRENT 时按 PIXELTYPE 推算返回（24 / 8 / 1），SET 时拒绝（避免与 PIXELTYPE 冲突）。

### 3.4 settings UI

`settings_server.cpp` 渲染：

```html
<select name="pixel_type">
  <option value="2" selected>Color (24-bit)</option>
  <option value="1">Gray (8-bit)</option>
  <option value="0">BW (1-bit)</option>
</select>
```

i18n 字符串 `pixel_color` / `pixel_gray` / `pixel_bw`，提交后 `caps_[ICAP_PIXELTYPE]` 更新。

### 3.5 像素格式转换实现

```cpp
void VirtualScanner::applyPixelFormat(const ScannerSettings& s) {
  if (!current_fibitmap_) return;
  FIBITMAP* dst = nullptr;
  switch (s.pixel_type) {
    case TWPT_RGB:
      if (FreeImage_GetBPP(current_fibitmap_) != 24) {
        dst = FreeImage_ConvertTo24Bits(current_fibitmap_);
      }
      break;
    case TWPT_GRAY: {
      dst = FreeImage_ConvertToGreyscale(current_fibitmap_);  // 输出 8 bpp
      break;
    }
    case TWPT_BW: {
      // 先确保 8-bit 灰度，再 Threshold 到 1-bit.
      FIBITMAP* gray = FreeImage_ConvertToGreyscale(current_fibitmap_);
      if (gray) {
        dst = FreeImage_Threshold(gray, 128);
        FreeImage_Unload(gray);
      }
      break;
    }
  }
  if (dst) {
    FreeImage_Unload(current_fibitmap_);
    current_fibitmap_ = dst;
  }
}
```

调用顺序固定：`applyPageSizeScaling` → `applyPixelFormat` → `applyDpiMetadata`。

### 3.6 Native Transfer DIB 构建

`twain_data_source.cpp::allocAndFillDibHeader()` 根据 `FreeImage_GetBPP(current_fibitmap_)` 选择 `biBitCount`，并准备调色板：

```cpp
WORD bpp = FreeImage_GetBPP(current_fibitmap_);
bih.biBitCount    = bpp;
bih.biCompression = BI_RGB;
bih.biSizeImage   = BYTES_PERLINE(width, bpp) * height;

DWORD palette_bytes = 0;
if (bpp == 1)      palette_bytes = sizeof(RGBQUAD) * 2;
else if (bpp == 8) palette_bytes = sizeof(RGBQUAD) * 256;

// 总大小 = sizeof(BITMAPINFOHEADER) + palette + pixels
```

palette 内容：

- 1-bit：`{0,0,0,0}` 黑 + `{255,255,255,0}` 白（chocolate）。
- 8-bit：256 项 `{i,i,i,0}`。

行数据从 `FreeImage_GetScanLine` 复制，按 `BYTES_PERLINE` 4 字节对齐，bottom-up 不变。

`getImageInfo()` 上报：

```cpp
info.BitsPerPixel = bpp;
info.SamplesPerPixel = (bpp == 24) ? 3 : 1;
info.BitsPerSample[0] = (bpp == 24) ? 8 : bpp;
info.PixelType = settings_.pixel_type;
info.Planar = TWPC_CHUNKY;  // BGR 交错 / 单通道
```

### 3.7 File Transfer 编码

`saveImageToFile()` 根据 `(image_file_format, current_fibitmap_ bpp)` 决定具体策略：

```cpp
switch (image_file_format) {
  case TWFF_PNG:  FreeImage_Save(FIF_PNG,  bmp, path, 0);                   break;
  case TWFF_BMP:  FreeImage_Save(FIF_BMP,  bmp, path, 0);                   break;
  case TWFF_TIFF: FreeImage_Save(FIF_TIFF, bmp, path,
                                 (bpp == 1) ? TIFF_CCITTFAX4 : TIFF_LZW);   break;
  case TWFF_JFIF: {
    FIBITMAP* to_save = bmp;
    FIBITMAP* fallback = nullptr;
    if (bpp == 1) {
      fallback = FreeImage_ConvertToGreyscale(bmp);  // JPEG 不支持 1-bit
      to_save = fallback;
    }
    FreeImage_Save(FIF_JPEG, to_save, path, JPEG_QUALITYGOOD);  // ≈85
    if (fallback) FreeImage_Unload(fallback);
    break;
  }
}
```

TIFF 在 1-bit 时使用 CCITT Group 4 压缩（文档扫描标准）；其余位深用 LZW。

保存后调用 `patchSavedDpiMetadata`（见 `file_dpi_design.md`），与位深无关，统一写 DPI。

## 4. 主要设计决策与原因

### 4.1 仅暴露 BW / Gray / RGB 三档

- 决策：`ICAP_PIXELTYPE` ENUMERATION 只包含 `{TWPT_BW, TWPT_GRAY, TWPT_RGB}`。
- 原因：覆盖 95% 真实扫描场景；其余如 `TWPT_PALETTE` / `TWPT_CMY` / `TWPT_CMYK` 在虚拟扫描仪上没有真实意义，加入会带来调色板生成、墨水分色等大量代码却没人用。

### 4.2 BW 默认用阈值化而不是抖动

- 决策：`FreeImage_Threshold(gray, 128)`。
- 原因：本项目主要用途是测试扫描应用 + OCR，阈值化保留文字边缘锐利；抖动 (Floyd-Steinberg) 会让 OCR 误判。如果未来需要照片型 BW，可在 settings UI 加一个 "BW mode: threshold / dither" 选项。

### 4.3 Gray 使用 `FreeImage_ConvertToGreyscale`，而不是 `ConvertTo8Bits`

- 决策：调 `ConvertToGreyscale`。
- 原因：`ConvertTo8Bits` 会用 Windows 半色调调色板，输出的 256 项 palette 不是连续灰阶，DIB 看上去像彩色噪点。`ConvertToGreyscale` 内部用 BT.601 权重生成 8-bit 灰度图并自动填灰阶调色板。

### 4.4 BW 转换走 "RGB → Gray → BW" 两步

- 决策：先 `ConvertToGreyscale` 再 `Threshold`。
- 原因：`FreeImage_Threshold` 要求输入是 8-bit 灰度或调色板图。从 24-bit 直接 `Threshold` 会失败或得到不可预期结果。两步路径明确、可靠。

### 4.5 像素类型转换永远晚于 DPI 重采样

- 决策：`preScanPrep` 强制顺序 `Rescale` → `applyPixelFormat`。
- 原因：见 §2.6 / `implement_dpi_design.md` §2.3。先量化再缩放会破坏 1-bit / 8-bit 的离散语义。

### 4.6 JPEG + BW 自动 fallback 到 8-bit 灰度

- 决策：保存 JPEG 时若 bpp == 1，先 `ConvertToGreyscale` 再 `FreeImage_Save`。
- 原因：JPEG 规范不支持 1-bit；若返回错误会让应用扫描中断。`TW_IMAGEINFO.PixelType` 仍上报 `TWPT_BW`（按用户请求），文件内部是 8-bit 灰度，但视觉上看起来仍是 BW（阈值化后的灰度只有 0 / 255 两个值）。这样应用既能拿到合法 JPEG，又看到约定的像素类型。

### 4.7 1-bit DIB 的 palette 固定写 chocolate

- 决策：palette[0] = 黑、palette[1] = 白。
- 原因：与 `ICAP_PIXELFLAVOR = TWPF_CHOCOLATE` 一致。如果未来支持 vanilla，则在保留像素位的同时翻转 palette 而不是翻转像素，避免双倍翻转。

### 4.8 TIFF 在 1-bit 时用 CCITT G4，其余用 LZW

- 决策：`(bpp == 1) ? TIFF_CCITTFAX4 : TIFF_LZW`。
- 原因：CCITT G4 是 1-bit 文档扫描的事实标准（传真、PDF/A 内嵌），压缩率 5~10x；LZW 对 8 / 24-bit 通用且无损。避免在 8-bit 上用 G4（不合法）或在 1-bit 上用 LZW（压缩率差很多）。

### 4.9 像素类型完全由 VirtualScanner 处理，DS 只读位深

- 决策：`TwainDataSource` 通过 `FreeImage_GetBPP(current_fibitmap_)` 判断 DIB 头位深和 palette；不再独立维护 pixel_type 渲染逻辑。
- 原因：避免双源真相。`current_fibitmap_` 已经是终态，DIB / 文件输出都从它派生，行为始终一致。

## 5. 架构各组件改动点

### 5.1 `src/capability.cpp`

- `ICAP_PIXELTYPE` ENUMERATION：默认 `TWPT_RGB`，值 `{TWPT_BW, TWPT_GRAY, TWPT_RGB}`，全套操作。
- `ICAP_PIXELFLAVOR` ONEVALUE：`TWPF_CHOCOLATE`。
- `ICAP_BITDEPTH` 在 GET / GETCURRENT / GETDEFAULT 时按 PIXELTYPE 推算返回；SET 返回 `TWCC_BADCAP` 或 `TWCC_SEQERROR`。
- `CAP_SUPPORTEDCAPS` 把上述能力加入数组。

### 5.2 `src/twain_data_source.cpp`

- `updateScannerFromCaps()` 把 `ICAP_PIXELTYPE` 写入 `settings_.pixel_type`。
- `handleDatImageInfo()`：从 `FreeImage_GetBPP(current_fibitmap_)` 推 `BitsPerPixel`、`SamplesPerPixel`、`BitsPerSample`，并按 `settings_.pixel_type` 报告 `PixelType`。
- `allocAndFillDibHeader()`：按 bpp 选择 palette 大小（0 / 2 / 256），写 RGBQUAD。
- `copyDibPixelData()`：按 `BYTES_PERLINE(width, bpp)` 计算行步长，bottom-up 复制。
- `getScanStrip()`：strip 大小按当前 bpp 行字节算，不再硬编码 24-bit。

### 5.3 `src/virtual_scanner.h/.cpp`

- 新增 `applyPixelFormat(const ScannerSettings&)`：实现三档转换 + BW 两步流水线。
- `preScanPrep()` 固化顺序 `acquireImage → ensure24BitDib → applyPageSizeScaling → applyPixelFormat → applyDpiMetadata`。
- `saveImageToFile()` 加 JPEG + 1-bit 的 fallback 分支，TIFF 按 bpp 选 CCITT G4 / LZW。
- 提供 helper：`bppFromPixelType(TW_UINT16) -> WORD`，避免散落 magic number。

### 5.4 `src/settings_server.cpp`

- pixel_type 下拉框 Color / Gray / BW，默认按 `caps_[ICAP_PIXELTYPE]` 当前值选中。
- i18n 字符串：`pixel_color_label` / `pixel_gray_label` / `pixel_bw_label`。
- 提交时把选项写回 `caps_[ICAP_PIXELTYPE]`。

### 5.5 测试影响

- 矩阵：3 种像素类型 × 4 种 DPI × 2 种 transfer × 4 种文件格式（File Transfer）。
- 重点用例：
  - BW + JPEG：验证 fallback 到灰度 JPEG，文件可打开，TW_IMAGEINFO.PixelType = TWPT_BW。
  - Gray + Native Transfer：DIB 调色板必须是 256 项灰阶。
  - BW + Native Transfer：DIB 行字节按 `((w+31)/32)*4`，palette = 2。
  - BW + TIFF：文件压缩用 CCITT G4。

## 6. 限制

- 只支持 BW / Gray / RGB，不支持 CMY / CMYK / YUV / Indexed。
- BW 仅支持固定阈值 128，不支持自适应阈值或 dither 选项（已有 follow-up）。
- Gray 采用 BT.601 权重，无法切换 BT.709 / 自定义。
- JPEG + BW 时文件实际位深为 8，与 `TW_IMAGEINFO.PixelType` 不严格一致；接受这个折衷。
- 不支持 16-bit Gray 或 48-bit RGB。
- 不支持单次扫描多通道独立输出（如同时给彩色 + 灰度）。
- `ICAP_BITDEPTH` 只读，应用不能单独覆盖。
- `ICAP_PIXELFLAVOR` 锁定 chocolate；vanilla 工作流当前不工作。

## 7. 下一步工作

- 在 settings UI 增加 "BW mode: threshold / dither" 选项，dither 用 `FreeImage_Dither(FID_FS)`。
- 阈值化的阈值改为可配置（slider 64..192），并支持自适应阈值（Otsu / Sauvola）。
- 支持 `TWPF_VANILLA`：保留像素，翻转 1-bit / 8-bit palette。
- 支持 16-bit Gray + 48-bit RGB（部分高端扫描应用要求）。
- BW + JPEG 增加用户偏好开关：fallback 到灰度，或拒绝该组合并返回明确错误。
- 自动化测试：每种像素类型扫描后用 Python PIL / Pillow 读回，断言 mode (`1` / `L` / `RGB`)、palette、像素维度。
- 在 `CHANGELOG.md` 中记录像素类型相关行为变更。

</details>

<details>
<summary>English</summary>

## 1. Requirements

The virtual scanner must produce images in the pixel type chosen by the user / application. TWAIN maps these to:

- `TWPT_RGB` — 24-bit true color (R / G / B, 8 bits each)
- `TWPT_GRAY` — 8-bit grayscale (0..255)
- `TWPT_BW`  — 1-bit bitonal (0 / 1, semantics defined by `ICAP_PIXELFLAVOR`)

Functional requirements:

- When an application sets `ICAP_PIXELTYPE`, the DS must actually convert pixels to the target format (not merely tag the image with the type while returning 24-bit).
- The settings UI offers Color / Gray / BW with behavior identical to capability-driven changes.
- Native Transfer DIBs (`BITMAPINFOHEADER` + palette + rows) must use the correct `biBitCount`:
  - RGB → 24
  - GRAY → 8 with 256-entry grayscale palette
  - BW → 1 with 2-entry black / white palette
- File Transfer must save PNG / JPG / BMP / TIFF with bit depth matching the pixel type. JPEG cannot encode 1-bit, so a fallback path is required.
- Pixel-type conversion must happen after DPI rescaling so that BW quantization is not smeared back into grayscale.
- Pixel type, DPI, and page size must be orthogonal — any combination must produce a coherent image and `TW_IMAGEINFO`.
- All output respects `ICAP_PIXELFLAVOR = TWPF_CHOCOLATE`: 0 = black, 1 = white.

Non-functional requirements:

- Conversion is as lossless as possible: BW via thresholding, Gray via perceptual luma, RGB pass-through.
- No new third-party dependencies beyond FreeImage.
- Each scan converts pixel type at most once.
- Output files must open correctly in Windows Photos, XnView, Photoshop, NAPS2.

## 2. Domain knowledge

### 2.1 TWAIN pixel types

`ICAP_PIXELTYPE` (`TW_UINT16`) common values:

| Value | Name | Meaning |
|---|---|---|
| 0 | `TWPT_BW`   | 1-bit bitonal |
| 1 | `TWPT_GRAY` | 8-bit grayscale |
| 2 | `TWPT_RGB`  | 24-bit true color |

Registered as an ENUMERATION with default `TWPT_RGB`.

`ICAP_BITDEPTH` is derived from `ICAP_PIXELTYPE`:

- RGB → 24, GRAY → 8, BW → 1.

Applications cannot set `ICAP_BITDEPTH` directly in this project; it is read-only and follows `ICAP_PIXELTYPE`.

`ICAP_PIXELFLAVOR` controls 0 / 1 semantics:

- `TWPF_CHOCOLATE`: 0 = black, 1 = white (project default).
- `TWPF_VANILLA`  : 0 = white, 1 = black.

### 2.2 DIB bit-depth and palette rules

Windows DIB hard requirements:

| biBitCount | Palette | Row layout |
|---|---|---|
| 24 | none | BGR BGR BGR ..., row padded to 4 bytes |
| 8  | 256-entry RGBQUAD required | one palette index per byte |
| 1  | 2-entry RGBQUAD required | one bit per pixel, MSB first; row padded to 4 bytes |

- 8-bit grayscale DIB palette: 256 entries `(i, i, i, 0)`.
- 1-bit DIB palette: index 0 = black `(0,0,0,0)`, index 1 = white `(255,255,255,0)`.
- Row stride: `((biWidth * biBitCount + 31) / 32) * 4`.

A DIB header that declares 8-bit without a palette is malformed.

### 2.3 Pixel types in FreeImage

| TWAIN | FreeImage | API |
|---|---|---|
| `TWPT_RGB`  | 24 bpp BGR (`FIT_BITMAP`) | `FreeImage_ConvertTo24Bits` |
| `TWPT_GRAY` | 8 bpp + 256 gray palette | `FreeImage_ConvertToGreyscale` |
| `TWPT_BW`   | 1 bpp + 2-entry palette  | `FreeImage_Threshold` / `FreeImage_Dither` |

Notes:

- `FreeImage_ConvertTo8Bits` uses a Windows halftone palette; for a true grayscale DIB use `FreeImage_ConvertToGreyscale`.
- `FreeImage_Threshold(src, T)` requires 8-bit input; T = 128 is a reasonable default.
- `FreeImage_Dither(src, FID_FS)` performs Floyd-Steinberg dithering — better for photos, worse for OCR.

### 2.4 Grayscale weights

- BT.601: `Y = 0.299 R + 0.587 G + 0.114 B`
- BT.709: `Y = 0.2126 R + 0.7152 G + 0.0722 B`

`FreeImage_ConvertToGreyscale` uses BT.601. For scanner test content the difference is negligible.

### 2.5 File-format support per pixel type

| Format | 1-bit | 8-bit Gray | 24-bit RGB |
|---|---|---|---|
| PNG  | yes | yes | yes |
| TIFF | yes (G4 / LZW / Raw) | yes | yes |
| BMP  | yes | yes | yes |
| JPEG | no  | yes | yes |

JPEG + BW requires either fallback to 8-bit gray, or rejection. The project picks fallback so applications never fail mid-scan, while still reporting `TW_IMAGEINFO.PixelType = TWPT_BW` to honor the negotiated contract.

### 2.6 Order vs. DPI rescaling

See `implement_dpi_design.md` §2.3: always rescale in 24-bit BGR first, then convert to the target pixel type. Quantizing before rescaling corrupts 1-bit / 8-bit semantics with intermediate grays.

## 3. Design

### 3.1 Data flow

```
[App / UI sets ICAP_PIXELTYPE]
        │
        ▼
TwainDataSource::handleDatCapability (MSG_SET pixel_type)
        │  stored in caps_
        ▼
TwainDataSource::updateScannerFromCaps()
        │  settings_.pixel_type = caps_[ICAP_PIXELTYPE]
        ▼
VirtualScanner::preScanPrep(ScannerSettings)
        │  acquireImage → ensure24BitDib
        │  applyPageSizeScaling (still in 24-bit BGR)
        │  applyPixelFormat(s.pixel_type) → 1 / 8 / 24 bpp
        │  applyDpiMetadata
        ▼
current_fibitmap_
        │
        ├──► Native: getDibImage → BITMAPINFOHEADER(biBitCount)
        │                          + palette
        │                          + rows (4-byte aligned)
        │
        └──► File: saveImageToFile → format-specific encoder
                                     (JPEG + 1-bit fallback)
```

### 3.2 ScannerSettings.pixel_type

```cpp
enum class PixelType : uint16_t {
  BW   = TWPT_BW,
  Gray = TWPT_GRAY,
  RGB  = TWPT_RGB,
};
```

### 3.3 Capability layer

```cpp
addCap(ICAP_PIXELTYPE, TWTY_UINT16, TWON_ENUMERATION,
       TWPT_RGB,
       {TWPT_BW, TWPT_GRAY, TWPT_RGB});

addCap(ICAP_PIXELFLAVOR, TWTY_UINT16, TWON_ONEVALUE,
       TWPF_CHOCOLATE, {TWPF_CHOCOLATE});
```

`ICAP_BITDEPTH` returns 24 / 8 / 1 for GET/GETCURRENT/GETDEFAULT based on the current `ICAP_PIXELTYPE`; SET is rejected.

### 3.4 Settings UI

```html
<select name="pixel_type">
  <option value="2" selected>Color (24-bit)</option>
  <option value="1">Gray (8-bit)</option>
  <option value="0">BW (1-bit)</option>
</select>
```

Submit writes back to `caps_[ICAP_PIXELTYPE]`.

### 3.5 Pixel conversion implementation

```cpp
void VirtualScanner::applyPixelFormat(const ScannerSettings& s) {
  if (!current_fibitmap_) return;
  FIBITMAP* dst = nullptr;
  switch (s.pixel_type) {
    case TWPT_RGB:
      if (FreeImage_GetBPP(current_fibitmap_) != 24) {
        dst = FreeImage_ConvertTo24Bits(current_fibitmap_);
      }
      break;
    case TWPT_GRAY:
      dst = FreeImage_ConvertToGreyscale(current_fibitmap_);
      break;
    case TWPT_BW: {
      FIBITMAP* gray = FreeImage_ConvertToGreyscale(current_fibitmap_);
      if (gray) {
        dst = FreeImage_Threshold(gray, 128);
        FreeImage_Unload(gray);
      }
      break;
    }
  }
  if (dst) {
    FreeImage_Unload(current_fibitmap_);
    current_fibitmap_ = dst;
  }
}
```

Pipeline order is fixed at `applyPageSizeScaling → applyPixelFormat → applyDpiMetadata`.

### 3.6 Native Transfer DIB construction

```cpp
WORD bpp = FreeImage_GetBPP(current_fibitmap_);
bih.biBitCount    = bpp;
bih.biCompression = BI_RGB;
bih.biSizeImage   = BYTES_PERLINE(width, bpp) * height;

DWORD palette_bytes = 0;
if (bpp == 1)      palette_bytes = sizeof(RGBQUAD) * 2;
else if (bpp == 8) palette_bytes = sizeof(RGBQUAD) * 256;
```

Palette content:

- 1-bit: `{0,0,0,0}` + `{255,255,255,0}` (chocolate).
- 8-bit: 256 entries `{i,i,i,0}`.

Rows are copied bottom-up with `BYTES_PERLINE` 4-byte alignment.

`getImageInfo()`:

```cpp
info.BitsPerPixel    = bpp;
info.SamplesPerPixel = (bpp == 24) ? 3 : 1;
info.BitsPerSample[0]= (bpp == 24) ? 8 : bpp;
info.PixelType       = settings_.pixel_type;
info.Planar          = TWPC_CHUNKY;
```

### 3.7 File Transfer encoding

```cpp
switch (image_file_format) {
  case TWFF_PNG:  FreeImage_Save(FIF_PNG,  bmp, path, 0);                   break;
  case TWFF_BMP:  FreeImage_Save(FIF_BMP,  bmp, path, 0);                   break;
  case TWFF_TIFF: FreeImage_Save(FIF_TIFF, bmp, path,
                                 (bpp == 1) ? TIFF_CCITTFAX4 : TIFF_LZW);   break;
  case TWFF_JFIF: {
    FIBITMAP* to_save = bmp;
    FIBITMAP* fallback = nullptr;
    if (bpp == 1) {
      fallback = FreeImage_ConvertToGreyscale(bmp);
      to_save = fallback;
    }
    FreeImage_Save(FIF_JPEG, to_save, path, JPEG_QUALITYGOOD);
    if (fallback) FreeImage_Unload(fallback);
    break;
  }
}
```

After saving, `patchSavedDpiMetadata` (see `file_dpi_design.md`) writes container-level DPI regardless of bit depth.

## 4. Key decisions and rationale

### 4.1 Expose only BW / Gray / RGB

- Decision: ENUMERATION values `{TWPT_BW, TWPT_GRAY, TWPT_RGB}`.
- Rationale: Covers the vast majority of real scanner workflows; CMY / CMYK / Palette have no value in a virtual scanner and would add significant code paths.

### 4.2 BW uses threshold by default

- Decision: `FreeImage_Threshold(gray, 128)`.
- Rationale: Sharper text edges; better OCR fidelity. Dither stays available for a future setting.

### 4.3 Gray uses `FreeImage_ConvertToGreyscale`, not `ConvertTo8Bits`

- Decision: Use the grayscale-specific API.
- Rationale: `ConvertTo8Bits` may produce a halftone palette, not contiguous gray entries, producing apparent "color noise" in the DIB.

### 4.4 BW pipeline is two-step

- Decision: `RGB → Gray (8-bit) → Threshold (1-bit)`.
- Rationale: `FreeImage_Threshold` requires 8-bit input; chaining via grayscale is predictable and matches FreeImage's intended use.

### 4.5 Pixel conversion happens after DPI rescaling

- Decision: Fixed `Rescale → applyPixelFormat` order.
- Rationale: Quantizing before rescaling re-introduces grays into 1-bit data; rescaling already-quantized 8-bit gray destroys edges.

### 4.6 JPEG + BW falls back to 8-bit grayscale

- Decision: If `bpp == 1` and target is JPEG, convert to grayscale before save.
- Rationale: JPEG cannot encode 1-bit. Failing the scan is worse than fallback. `TW_IMAGEINFO.PixelType` still reports BW per the contract.

### 4.7 1-bit DIB palette is fixed to chocolate semantics

- Decision: palette[0] = black, palette[1] = white.
- Rationale: Matches `ICAP_PIXELFLAVOR = TWPF_CHOCOLATE`. A future vanilla mode would swap palette entries, not invert pixels.

### 4.8 TIFF: CCITT G4 for 1-bit, LZW otherwise

- Decision: `(bpp == 1) ? TIFF_CCITTFAX4 : TIFF_LZW`.
- Rationale: CCITT G4 is the canonical 1-bit document codec (fax / PDF/A). LZW is general-purpose lossless for 8/24-bit.

### 4.9 Pixel-type work owned by VirtualScanner; DS reads bpp only

- Decision: `TwainDataSource` derives DIB layout from `FreeImage_GetBPP(current_fibitmap_)`.
- Rationale: Single source of truth. Both Native and File Transfer derive from the same final bitmap and cannot diverge.

## 5. Architectural component changes

### 5.1 `src/capability.cpp`

- `ICAP_PIXELTYPE` ENUMERATION (default RGB, values BW / Gray / RGB), all operations.
- `ICAP_PIXELFLAVOR` ONEVALUE `TWPF_CHOCOLATE`.
- `ICAP_BITDEPTH` derived from PIXELTYPE on GET; SET returns `TWCC_BADCAP`.
- `CAP_SUPPORTEDCAPS` includes the above.

### 5.2 `src/twain_data_source.cpp`

- `updateScannerFromCaps()` propagates pixel type into `settings_.pixel_type`.
- `handleDatImageInfo()` derives `BitsPerPixel`, `SamplesPerPixel`, `BitsPerSample[]` from `FreeImage_GetBPP`; reports `PixelType` from settings.
- `allocAndFillDibHeader()` chooses palette size (0 / 2 / 256), writes RGBQUADs.
- `copyDibPixelData()` computes row stride via `BYTES_PERLINE(width, bpp)`, bottom-up.
- `getScanStrip()` uses the current bpp for strip sizing.

### 5.3 `src/virtual_scanner.h/.cpp`

- Add `applyPixelFormat(const ScannerSettings&)`.
- Fix pipeline order in `preScanPrep`.
- `saveImageToFile()` adds JPEG + BW fallback and TIFF compression selection.
- Helper `bppFromPixelType(TW_UINT16)`.

### 5.4 `src/settings_server.cpp`

- pixel_type dropdown with Color / Gray / BW.
- i18n labels.
- Submit writes `caps_[ICAP_PIXELTYPE]`.

### 5.5 Test impact

- Matrix: 3 pixel types × 4 DPIs × 2 transfer modes × 4 file formats.
- Focus cases:
  - BW + JPEG → fallback path, file opens, `TW_IMAGEINFO.PixelType == TWPT_BW`.
  - Gray + Native → 256-entry grayscale palette in DIB.
  - BW + Native → row stride `((w+31)/32)*4`, 2-entry palette.
  - BW + TIFF → CCITT G4 compression.

## 6. Limitations

- Only BW / Gray / RGB supported; CMY / CMYK / YUV / palette unsupported.
- BW threshold fixed at 128; no adaptive threshold or dither switch yet.
- Gray uses BT.601 only.
- JPEG + BW writes 8-bit grayscale on disk while reporting BW; accepted trade-off.
- 16-bit gray and 48-bit RGB not supported.
- No multi-channel concurrent output.
- `ICAP_BITDEPTH` is read-only.
- `TWPF_VANILLA` not supported.

## 7. Next steps

- Add "BW mode: threshold / dither" UI option (`FreeImage_Dither(FID_FS)`).
- Configurable BW threshold (64..192) and adaptive thresholds (Otsu / Sauvola).
- Support `TWPF_VANILLA` by swapping palette entries.
- Add 16-bit gray and 48-bit RGB for high-end workflows.
- Provide a user-visible preference for BW + JPEG: fallback vs. reject.
- Automated tests: read back each saved file with Pillow and assert `mode` (`1` / `L` / `RGB`), palette, and pixel dimensions.
- Record pixel-type-related behavior changes in `CHANGELOG.md`.

</details>
