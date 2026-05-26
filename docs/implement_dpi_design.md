# Implement Different DPI Output Image Design

Design notes for actually producing image data at different DPI (150 / 200 / 300 / 600) for both Native Transfer and File Transfer, end-to-end from capability negotiation through FreeImage rescaling to DIB / file output.

<details open>
<summary>中文说明</summary>

## 1. 需求

虚拟扫描仪需要能够按用户选择的 DPI（150 / 200 / 300 / 600 之一）输出图像，让上层应用看到的像素尺寸与"按选定 DPI 扫描指定纸张"得到的结果一致，而不仅仅是改 DPI 元数据字段。

主要需求：

- TWAIN 应用通过 `ICAP_XRESOLUTION` / `ICAP_YRESOLUTION` 设定 DPI 时，DS 必须真实地按目标 DPI 重采样输出图像，使输出像素宽高 = `纸张英寸数 × DPI`。
- settings UI 中用户从下拉框选择 150 / 200 / 300 / 600 时，行为与应用通过能力设置时一致。
- 必须同时支持 Native Transfer 和 File Transfer 两条路径，二者必须输出相同像素的图像。
- 必须同时支持三种像素模式（`TWPT_BW` / `TWPT_GRAY` / `TWPT_RGB`），DPI 切换不能与像素模式切换互相破坏。
- `TW_IMAGEINFO.XResolution` / `YResolution` 和 DIB 头 `biXPelsPerMeter` / `biYPelsPerMeter` 以及输出文件元数据必须报告与实际像素密度一致的 DPI。
- 支持页面尺寸（A4 / Letter / 自定义等）与 DPI 的组合，目标像素宽高由二者共同决定。
- 重采样质量必须足够好，不能出现明显锯齿或马赛克；缩小不允许 nearest-neighbor 严重模糊文字。

非功能性需求：

- 一次扫描只允许重采样一次，避免对同一图像反复 `FreeImage_Rescale`。
- 当源图像本身分辨率比目标小（例如源 800×600，目标 A4@600DPI 需要约 4960×7016）时，仍然必须放大，不允许直接拒绝或返回原图。
- 单页扫描的 DPI 切换不依赖磁盘缓存，每次都从原图重新算，避免上一次设置影响下一次。

## 2. 领域知识

### 2.1 TWAIN 中 DPI 与图像尺寸的关系

TWAIN 把扫描分辨率表达成 `ICAP_XRESOLUTION` / `ICAP_YRESOLUTION`（FIX32，单位由 `ICAP_UNITS` 决定）。本项目固定 `ICAP_UNITS = TWUN_INCHES`，所以这两个能力的值就是 DPI。

应用看到的图像像素尺寸由三件事共同决定：

- 扫描区域大小（英寸）：本项目用 `ICAP_SUPPORTEDSIZES` 表达，UI 选择 A4 / Letter / Custom 之后会映射成 `CustomPageSize`（宽高，英寸）。
- 水平和垂直 DPI：`ICAP_XRESOLUTION` / `ICAP_YRESOLUTION`。
- 像素格式（BW / GRAY / RGB）只影响每像素位数，不影响宽高。

因此 `TW_IMAGEINFO.ImageWidth = round(page_width_inch × XResolution)`，`ImageLength = round(page_height_inch × YResolution)`。所有 DS 必须保证返回的图像像素数和这个公式一致，否则应用拼版、缩放、OCR 都会失准。

### 2.2 FreeImage 重采样

FreeImage 提供 `FIBITMAP* FreeImage_Rescale(FIBITMAP*, int dst_w, int dst_h, FREE_IMAGE_FILTER filter)`：

- 支持 `FILTER_BOX` / `FILTER_BILINEAR` / `FILTER_BICUBIC` / `FILTER_LANCZOS3` 等滤波器。
- 输入输出像素格式一致：传入 24-bit BGR 返回 24-bit BGR，传入 8-bit 灰度返回 8-bit 灰度。
- 返回新 FIBITMAP，必须负责释放旧的。

对扫描类内容（文档、照片混合）一般选 `FILTER_LANCZOS3` 或 `FILTER_BICUBIC` 比较稳：`LANCZOS3` 锐度高、细节好；`BICUBIC` 平滑、不易振铃。本项目选 `FILTER_LANCZOS3` 作为默认。

### 2.3 像素格式转换与 DPI 的顺序

像素格式转换（24-bit RGB → 8-bit 灰度 / 1-bit 黑白）通常应该在最终目标分辨率上做，不要在原始分辨率上转完再重采样：

- 1-bit 黑白做完 `FreeImage_Threshold` 后再 `FreeImage_Rescale` 会引入大量灰度像素，与 1-bit 不兼容，需要再重新二值化。
- 8-bit 灰度先做 `FreeImage_Rescale` 不会出现颜色异常，但灰度阈值后处理会更脆弱。

合理的顺序：

```
源 RGB → 24-bit BGR → Rescale 到目标 (w,h) → 按 pixel type 转 (BW / GRAY / RGB) → 写 DPI 元数据
```

### 2.4 页面尺寸缩放模式

源图像和"纸张尺寸 × DPI"算出的目标像素宽高比例不一致时，需要决定如何填充：

- Stretch：直接非等比缩放到目标 (w,h)，简单，但形变。
- Fit：保持宽高比缩到能放进目标，剩余区域用背景色填充（虚拟扫描仪用白色，模拟纸张）。
- Fill：保持宽高比放大到铺满目标，超出部分裁掉。

这三种由 `ScannerSettings.page_fill_mode` 控制。无论哪种模式，最终交给 TWAIN 的图像尺寸都必须是 `page_width_inch × DPI` 和 `page_height_inch × DPI`。

### 2.5 DPI 元数据与像素 DPI 必须一致

实现"按 DPI 出图"和"在文件里写 DPI 字段"是两件事，但二者必须一致：

- 像素维度按 DPI 重采样 → 决定 `TW_IMAGEINFO`、DIB 宽高、文件像素宽高。
- 元数据写 DPI → 决定 Windows Explorer / Photoshop 显示的物理分辨率。

如果只改元数据不重采样，Photoshop 会显示"图像 3 × 4 英寸 @ 600DPI"但实际像素只有 1800×2400 显示对，1024×768 显示就会是 1.7 × 1.3 英寸 @ 600DPI。两边必须由 DS 同步保证。

## 3. 设计

### 3.1 数据流概览

```
[App or UI sets DPI]
        │
        ▼
TwainDataSource::handleDatCapability  (ICAP_XRESOLUTION / YRESOLUTION SET)
        │  写入 caps_ 容器
        ▼
TwainDataSource::updateScannerFromCaps()
        │  从 caps_ 读 DPI / page size / pixel type
        │  填充 ScannerSettings { x_resolution, y_resolution,
        │                         page_size, pixel_type, ... }
        ▼
VirtualScanner::preScanPrep(ScannerSettings)
        │  1. acquireImage()           从 images/ 加载或回退 logo
        │  2. ensure24BitDib()         统一到 24-bit BGR
        │  3. applyPageSizeScaling()   FreeImage_Rescale 到
        │                              (page_w × DPI, page_h × DPI)
        │  4. applyPixelFormat()       Rescale 之后按 pixel_type 转
        │  5. applyDpiMetadata()       写 FIBITMAP 的 DPI 元数据
        ▼
current_fibitmap_ (已是目标 DPI 的目标格式)
        │
        ├──► Native Transfer: getDibImage() → BITMAPINFOHEADER.biXPelsPerMeter
        │                                     按 DPI 算（dpi × 39.37）
        │                                     行数据按已缩放后的宽高输出
        │
        └──► File Transfer:   saveImageToFile() → FreeImage_Save
                                                  patchSavedDpiMetadata()
```

### 3.2 ScannerSettings 字段

`virtual_scanner.h` 中 `ScannerSettings` 至少携带：

```cpp
struct ScannerSettings {
  TW_UINT16 pixel_type;        // TWPT_BW / TWPT_GRAY / TWPT_RGB
  double    x_resolution;      // DPI, 默认 300
  double    y_resolution;      // DPI, 默认 300
  PageSize  page_size;         // A4 / Letter / Custom (英寸)
  PageFillMode page_fill_mode; // Stretch / Fit / Fill
  // ...
};
```

`x_resolution` / `y_resolution` 直接以 DPI 表示（不是 FIX32），从 caps 读出的 FIX32 转 double 之后写入。

### 3.3 capability 层

`capability.cpp` 注册 `ICAP_XRESOLUTION` / `ICAP_YRESOLUTION` 为 `ENUMERATION` 容器：

- 类型 `TW_FIX32`
- 默认 `300`
- 可选 `{150, 200, 300, 600}`
- 支持 `GET / GETCURRENT / GETDEFAULT / SET / RESET` 全套操作

`MSG_SET` 时校验是否在可选集合内，不在则返回 `TWRC_FAILURE / TWCC_BADVALUE`，避免应用传入不支持的 DPI。

### 3.4 settings UI 行为

`settings_server.cpp` 在 HTML 中输出 DPI 下拉框：

```html
<select name="dpi">
  <option value="150">150</option>
  <option value="200">200</option>
  <option value="300" selected>300</option>
  <option value="600">600</option>
</select>
```

用户点 **Scan** 提交表单后，服务端把 dpi 写回 `caps_`（同时设 `ICAP_XRESOLUTION` 和 `ICAP_YRESOLUTION`），然后 `updateScannerFromCaps()` 推到 `ScannerSettings`。

### 3.5 重采样实现

```cpp
void VirtualScanner::applyPageSizeScaling(const ScannerSettings& s) {
  if (!current_fibitmap_) return;

  int src_w = FreeImage_GetWidth(current_fibitmap_);
  int src_h = FreeImage_GetHeight(current_fibitmap_);

  double page_w_inch = s.page_size.width_inch;
  double page_h_inch = s.page_size.height_inch;

  int dst_w = static_cast<int>(std::round(page_w_inch * s.x_resolution));
  int dst_h = static_cast<int>(std::round(page_h_inch * s.y_resolution));

  if (dst_w <= 0 || dst_h <= 0) return;

  FIBITMAP* dst = nullptr;
  switch (s.page_fill_mode) {
    case PageFillMode::Stretch:
      dst = FreeImage_Rescale(current_fibitmap_, dst_w, dst_h, FILTER_LANCZOS3);
      break;
    case PageFillMode::Fit:
      dst = RescaleFit(current_fibitmap_, dst_w, dst_h);   // 等比 + 白边
      break;
    case PageFillMode::Fill:
      dst = RescaleFill(current_fibitmap_, dst_w, dst_h);  // 等比 + 居中裁切
      break;
  }
  if (dst) {
    FreeImage_Unload(current_fibitmap_);
    current_fibitmap_ = dst;
  }
}
```

`Fit` 用 `FreeImage_Allocate(dst_w, dst_h, 24)` 填白色背景，再算等比缩放后的子图，`FreeImage_Paste` 到中央。
`Fill` 用 `FreeImage_Rescale` 到等比超出尺寸，再 `FreeImage_Copy` 居中裁剪到 `(dst_w, dst_h)`。

### 3.6 元数据写入

重采样完成后调用：

```cpp
FreeImage_SetDotsPerMeterX(current_fibitmap_,
                           static_cast<unsigned>(s.x_resolution * 39.37));
FreeImage_SetDotsPerMeterY(current_fibitmap_,
                           static_cast<unsigned>(s.y_resolution * 39.37));
```

这样：

- DIB 复制时按 `biXPelsPerMeter = FreeImage_GetDotsPerMeterX(...)` 写出。
- `FreeImage_Save` 写 PNG / TIFF / BMP 时也会以此为基础写元数据。
- 文件层 patcher (见 `file_dpi_design.md`) 在 FreeImage 写完后再按 settings 中的 DPI 强制覆写关键字段。

### 3.7 Native Transfer 的 DPI 报告

`twain_data_source.cpp::getImageInfo()`：

```cpp
info.XResolution = floatToFix32(settings_.x_resolution);
info.YResolution = floatToFix32(settings_.y_resolution);
info.ImageWidth  = FreeImage_GetWidth(current_fibitmap_);
info.ImageLength = FreeImage_GetHeight(current_fibitmap_);
```

DIB 头：

```cpp
bih.biXPelsPerMeter = static_cast<LONG>(settings_.x_resolution * 39.37);
bih.biYPelsPerMeter = static_cast<LONG>(settings_.y_resolution * 39.37);
```

### 3.8 File Transfer 的 DPI 报告

`saveImageToFile()` 在 `FreeImage_Save` 之前已经按 settings 写好了 DPI 元数据。保存完成再调用 `patchSavedDpiMetadata(path, x_dpi, y_dpi)`（详见 `file_dpi_design.md`），保证：

- PNG `pHYs`
- JFIF APP0 density
- BMP `biXPelsPerMeter` / `biYPelsPerMeter`
- TIFF `XResolution` / `YResolution` / `ResolutionUnit`

四种格式都以 `ScannerSettings.x_resolution / y_resolution` 为准。

## 4. 主要设计决策与原因

### 4.1 真正按 DPI 重采样，而不是只改元数据

- 决策：在 `applyPageSizeScaling` 中按 `(page_w_inch × DPI, page_h_inch × DPI)` 重采样像素。
- 原因：扫描应用（XnView 的"扫描到 PDF"、NAPS2、专业扫描中间件）会按 `TW_IMAGEINFO.ImageWidth / ImageLength` 排版，如果只改元数据，所有像素仍然是源尺寸，应用会显示 "300 DPI A4" 却只有缩略图大小的像素，OCR 也会拿不到足够分辨率。

### 4.2 DPI 可选值固定枚举 (150 / 200 / 300 / 600)

- 决策：`ICAP_XRESOLUTION` / `YRESOLUTION` 用 `ENUMERATION` 而非 `RANGE`。
- 原因：测试矩阵明确；UI 下拉框简单；避免应用传入不合理 DPI（如 75 或 4800）导致 600 DPI A4 重采样到 19840×28057 的极端 case。后续若需要可加进枚举。

### 4.3 重采样滤波器选 `FILTER_LANCZOS3`

- 决策：`FreeImage_Rescale` 统一使用 `FILTER_LANCZOS3`。
- 原因：文档类内容需要锐度，纯 `BILINEAR` 太糊；`BICUBIC` 折中但小字会发虚；`LANCZOS3` 是 FreeImage 内支持的最高质量滤波，性能在虚拟扫描仪场景完全够用（每次只处理一张）。

### 4.4 重采样发生在像素格式转换之前

- 决策：先 `Rescale`，再按 `pixel_type` 转 BW / GRAY / RGB。
- 原因：见 §2.3，先量化再缩放会破坏 1-bit / 8-bit 阈值，得到的图像质量差。统一在 24-bit BGR 域做缩放，再一次性二值化或灰度化。

### 4.5 缩放和元数据由 `VirtualScanner` 完成，DS 只负责打包

- 决策：`TwainDataSource` 不直接调 FreeImage，只在 `preScanPrep` 时把 `ScannerSettings` 交给 `VirtualScanner`，从 `current_fibitmap_` 取最终图像。
- 原因：单一职责。DS 关注 TWAIN 协议、状态机、DSM；VirtualScanner 关注像素和文件。这样 Native / File Transfer 共用同一份"按 DPI 缩放好的 FIBITMAP"，避免两条路径各自缩放产生不一致。

### 4.6 `page_w_inch × DPI` 用 `round` 而不是 `floor`

- 决策：用 `std::round`。
- 原因：避免 A4@600 这种宽度 4960.629... 被 `floor` 截到 4960 后 4960/600 = 8.2666... 英寸，导致元数据 DPI 与像素尺寸算回来的页面尺寸不严格相等。`round` 让宽高更接近名义值。

### 4.7 三种 page_fill_mode 而不是固定一种

- 决策：暴露 Stretch / Fit / Fill。
- 原因：测试场景多样：开发者验证 OCR 需要 Stretch（不挑像素），UI 测试需要 Fit（保留原图比例 + 白边模拟真纸），破坏性测试需要 Fill（看应用如何处理出血裁切）。三种都简单实现，没必要削减到一种。

### 4.8 缩放和元数据一次性、扫描前完成

- 决策：在 `preScanPrep` 一次性把 `current_fibitmap_` 准备成最终状态；strip / 行复制阶段不再触碰图像。
- 原因：Native Transfer 的 strip 循环会多次回调 `getScanStrip`，如果在 strip 阶段才缩放，性能差且会重复缩放。提前完成也让 `getImageInfo` 一次给出真实宽高。

## 5. 架构各组件改动点

### 5.1 `src/capability.cpp`

- 注册 `ICAP_XRESOLUTION` / `ICAP_YRESOLUTION` 为 ENUMERATION，默认 300，枚举 `{150,200,300,600}`。
- `MSG_SET` 时校验 FIX32 是否在集合内，不在则 `TWCC_BADVALUE`。
- 与 `ICAP_UNITS = TWUN_INCHES` 联动，确保单位语义稳定。

### 5.2 `src/twain_data_source.cpp`

- `updateScannerFromCaps()` 把 `ICAP_XRESOLUTION` / `YRESOLUTION` 的 FIX32 转 double，写入 `ScannerSettings.x_resolution / y_resolution`。
- `handleDatImageInfo()` 用 `settings_.x_resolution / y_resolution` 填 `TW_IMAGEINFO.XResolution / YResolution`，宽高用 `FreeImage_GetWidth / GetHeight`（已经是缩放后的目标值）。
- `allocAndFillDibHeader()` 用 `settings_.x_resolution * 39.37` 填 `biXPelsPerMeter / biYPelsPerMeter`。
- `enableDs()` 在用户提交 UI 后调用 `updateScannerFromCaps()` + `virtual_scanner_.preScanPrep(settings_)`。

### 5.3 `src/virtual_scanner.h/.cpp`

- 增加 `applyPageSizeScaling(const ScannerSettings&)`：按 (page_size, x/y_resolution, page_fill_mode) 调 `FreeImage_Rescale` / 自实现 `RescaleFit` / `RescaleFill`。
- 增加 `applyDpiMetadata(const ScannerSettings&)`：调 `FreeImage_SetDotsPerMeterX/Y`。
- 重构 `preScanPrep()`：`acquireImage → ensure24BitDib → applyPageSizeScaling → applyPixelFormat → applyDpiMetadata`，保证顺序固定。
- `saveImageToFile()` 不再二次缩放，只 `FreeImage_Save` + `patchSavedDpiMetadata`。

### 5.4 `src/settings_server.cpp`

- DPI 下拉框：150 / 200 / 300 / 600，默认 300，从 `caps_` 当前值同步选中。
- 提交时把 dpi 同时写 `ICAP_XRESOLUTION` 和 `ICAP_YRESOLUTION`。
- i18n：DPI 标签使用 `dpi_label` 等本地化字符串。

### 5.5 测试影响

- 测试用例需要覆盖 4 种 DPI × 3 种 pixel type × 2 种 transfer mode × 至少 2 种 page size 的组合矩阵中的关键样本。
- 输出文件用脚本验证：像素宽高 = `round(page_w * dpi)`、`round(page_h * dpi)`；DPI 字段与 settings 一致。

## 6. 限制

- 当前只支持 150 / 200 / 300 / 600，应用如果传其他值会被拒绝 (`TWCC_BADVALUE`)。
- 上采样 (低分辨率源 → 高 DPI 大图) 会显著放大模糊；建议 `images/` 目录放高分辨率原图。
- `LANCZOS3` 滤波器在极端尺寸（如 9920×14040 600DPI A3）下内存和 CPU 占用明显，单次扫描可能耗时 1~2 秒；目前没有并行优化。
- `page_size` 当前只支持几种标准纸张和 Custom，不支持运行时任意像素裁切框 (`ICAP_FRAMES`)。
- `x_resolution` 与 `y_resolution` 在 UI 中只暴露同一档；通过 caps 单独设非对称 DPI 是合法的，但 UI 不会显示这一点。
- DPI 改变只触发图像层重采样，不重新加载源文件；如果中途换源图，需要重新进入扫描流程。
- 极端组合（如 Custom 0.5×0.5 英寸 @ 150 DPI = 75×75 像素）应用拿到的图像会非常小，目前不做最小尺寸限制。

## 7. 下一步工作

- 把 DPI 可选集扩展到 `{75, 100, 150, 200, 300, 400, 600, 1200}` 或改为 RANGE，方便压力测试。
- 在 `VirtualScanner` 加缓存：相同 `(source_image, page_size, DPI, fill_mode)` 命中时直接复用上一次 `current_fibitmap_`。
- 加单元测试 / 集成脚本：扫描后自动用 Python PIL 验证像素宽高和 DPI 元数据。
- 支持 `ICAP_FRAMES`，让应用按英寸坐标自定义裁切框，与 DPI 共同决定输出像素。
- 实现 X / Y 不对称 DPI 的真实路径（已在数据结构里支持，需在 UI 暴露 "advanced" 开关）。
- 评估更快的重采样路径（如把 Lanczos 改为多线程或在小缩放比时回退到 Bicubic）。
- 增加 ADF + 多页场景下，每页的 DPI 一致性测试。

</details>

<details>
<summary>English</summary>

## 1. Requirements

The virtual scanner must produce real image pixels at the user-selected DPI (150 / 200 / 300 / 600), not merely tag the output with DPI metadata. The pixel dimensions delivered to the application must equal `page_inches × DPI` for the chosen page size.

Functional requirements:

- When a TWAIN application sets `ICAP_XRESOLUTION` / `ICAP_YRESOLUTION`, the DS must rescale the output so that the returned pixel width / height equals `page_inches × DPI`.
- The settings UI must offer DPI choices 150 / 200 / 300 / 600 with identical behavior to capability-driven changes.
- Both Native Transfer and File Transfer paths must return identical pixels for the same settings.
- All three pixel types (`TWPT_BW` / `TWPT_GRAY` / `TWPT_RGB`) must compose cleanly with any DPI choice.
- `TW_IMAGEINFO.XResolution` / `YResolution`, DIB `biXPelsPerMeter` / `biYPelsPerMeter`, and saved-file metadata must report DPI consistent with the actual pixel density.
- Page size (A4 / Letter / Custom / ...) must combine with DPI to drive the final pixel dimensions.
- Resampling quality must be high enough that downscaled text remains readable and upscaled photos do not show severe blocking.

Non-functional requirements:

- Each scan rescales at most once; subsequent strip / row reads do not re-touch pixels.
- Low-resolution source images must still be upscaled to the target DPI; refusing or returning the original size is not allowed.
- DPI changes must not depend on disk caches; each scan computes from the latest source image and settings.

## 2. Domain knowledge

### 2.1 TWAIN DPI and image dimensions

TWAIN expresses scanner resolution through `ICAP_XRESOLUTION` / `ICAP_YRESOLUTION` (FIX32, unit driven by `ICAP_UNITS`). This project pins `ICAP_UNITS = TWUN_INCHES`, so the FIX32 value is literally DPI.

The pixel size seen by the application is determined by three factors:

- Scan area (inches): driven by `ICAP_SUPPORTEDSIZES`, which the UI maps to a `CustomPageSize` (width and height in inches).
- Horizontal and vertical DPI from the resolution capabilities.
- Pixel type only affects bits-per-pixel, not width / height.

Therefore `TW_IMAGEINFO.ImageWidth = round(page_width_inch × XResolution)` and `ImageLength = round(page_height_inch × YResolution)`. A correct DS must enforce this identity, otherwise applications doing imposition, OCR, or scaling will produce incorrect results.

### 2.2 FreeImage rescaling

FreeImage offers `FIBITMAP* FreeImage_Rescale(FIBITMAP*, int dst_w, int dst_h, FREE_IMAGE_FILTER filter)`:

- Filters include `FILTER_BOX`, `FILTER_BILINEAR`, `FILTER_BICUBIC`, `FILTER_LANCZOS3`, etc.
- Input and output share the same pixel format; the caller owns both the old and the new bitmap.

For mixed document / photo scanning content, `FILTER_LANCZOS3` and `FILTER_BICUBIC` are reasonable defaults. The project picks `FILTER_LANCZOS3` for sharper text.

### 2.3 Pixel-format conversion order

Pixel-format conversion (24-bit RGB → 8-bit gray → 1-bit BW) should happen at the final target resolution, not before resampling:

- Quantizing to 1-bit before `Rescale` then resampling introduces grayscale pixels that no longer match the 1-bit grammar and require re-thresholding.
- Quantizing to 8-bit gray before `Rescale` is less destructive but still loses fidelity around edges.

Canonical pipeline:

```
Source RGB → 24-bit BGR → Rescale to target (w,h) → convert to pixel_type → write DPI metadata
```

### 2.4 Page-size fill modes

When source aspect ratio differs from `page_w × DPI : page_h × DPI`, three policies are available:

- Stretch: non-uniform scale to (w,h). Simple but distorts.
- Fit: uniform scale to fit, fill the rest with white (simulating paper).
- Fill: uniform scale to cover, crop overflow.

Selected through `ScannerSettings.page_fill_mode`. Regardless of policy, the final image handed to TWAIN must be exactly `page_w × DPI` by `page_h × DPI`.

### 2.5 DPI metadata and pixel DPI must match

Resampling-to-DPI and writing-DPI-metadata are two independent steps but they must agree:

- Pixel resampling determines `TW_IMAGEINFO`, DIB width / height, and file pixel dimensions.
- Metadata determines what Explorer / Photoshop displays as the physical size.

If only metadata is changed, Photoshop will claim a paper-sized image but actually contain too few pixels, and consumers like OCR engines will silently underperform.

## 3. Design

### 3.1 Data flow

```
[App or UI sets DPI]
        │
        ▼
TwainDataSource::handleDatCapability (ICAP_XRESOLUTION / YRESOLUTION SET)
        │  stores into caps_
        ▼
TwainDataSource::updateScannerFromCaps()
        │  reads DPI / page size / pixel type from caps_
        │  populates ScannerSettings
        ▼
VirtualScanner::preScanPrep(ScannerSettings)
        │  1. acquireImage()
        │  2. ensure24BitDib()
        │  3. applyPageSizeScaling()   FreeImage_Rescale to (page_w*DPI, page_h*DPI)
        │  4. applyPixelFormat()       Convert to BW / GRAY / RGB
        │  5. applyDpiMetadata()       SetDotsPerMeterX/Y
        ▼
current_fibitmap_  (final DPI, final pixel format)
        │
        ├──► Native Transfer: getDibImage() → BITMAPINFOHEADER, pixels copied bottom-up
        │
        └──► File Transfer:   saveImageToFile() → FreeImage_Save → patchSavedDpiMetadata
```

### 3.2 ScannerSettings fields

```cpp
struct ScannerSettings {
  TW_UINT16 pixel_type;        // TWPT_BW / TWPT_GRAY / TWPT_RGB
  double    x_resolution;      // DPI, default 300
  double    y_resolution;      // DPI, default 300
  PageSize  page_size;         // A4 / Letter / Custom (inches)
  PageFillMode page_fill_mode; // Stretch / Fit / Fill
  // ...
};
```

DPI is stored as `double`; FIX32 values from caps are converted on the way in.

### 3.3 Capability layer

`capability.cpp` registers `ICAP_XRESOLUTION` / `ICAP_YRESOLUTION` as ENUMERATION:

- Type `TW_FIX32`, default 300, values `{150, 200, 300, 600}`.
- All operations: GET / GETCURRENT / GETDEFAULT / SET / RESET.
- `MSG_SET` rejects values outside the set with `TWRC_FAILURE / TWCC_BADVALUE`.

### 3.4 Settings UI

`settings_server.cpp` renders a DPI dropdown with the same four values, preselected from current caps. Submit writes the chosen DPI back to both X and Y resolution capabilities.

### 3.5 Rescaling

```cpp
void VirtualScanner::applyPageSizeScaling(const ScannerSettings& s) {
  if (!current_fibitmap_) return;
  int dst_w = static_cast<int>(std::round(s.page_size.width_inch  * s.x_resolution));
  int dst_h = static_cast<int>(std::round(s.page_size.height_inch * s.y_resolution));
  if (dst_w <= 0 || dst_h <= 0) return;

  FIBITMAP* dst = nullptr;
  switch (s.page_fill_mode) {
    case PageFillMode::Stretch:
      dst = FreeImage_Rescale(current_fibitmap_, dst_w, dst_h, FILTER_LANCZOS3);
      break;
    case PageFillMode::Fit:
      dst = RescaleFit(current_fibitmap_, dst_w, dst_h);
      break;
    case PageFillMode::Fill:
      dst = RescaleFill(current_fibitmap_, dst_w, dst_h);
      break;
  }
  if (dst) {
    FreeImage_Unload(current_fibitmap_);
    current_fibitmap_ = dst;
  }
}
```

`Fit` allocates a white 24-bit canvas, rescales the source preserving aspect, and pastes it centered. `Fill` rescales to cover and crops the overflow.

### 3.6 Metadata write

After rescaling and pixel-format conversion:

```cpp
FreeImage_SetDotsPerMeterX(current_fibitmap_,
                           static_cast<unsigned>(s.x_resolution * 39.37));
FreeImage_SetDotsPerMeterY(current_fibitmap_,
                           static_cast<unsigned>(s.y_resolution * 39.37));
```

Downstream:

- DIB header copies `biXPelsPerMeter` from this value.
- `FreeImage_Save` uses it to write PNG / BMP / TIFF metadata.
- Byte-level patchers (see `file_dpi_design.md`) then enforce exact DPI on the saved file.

### 3.7 Native Transfer DPI reporting

```cpp
info.XResolution = floatToFix32(settings_.x_resolution);
info.YResolution = floatToFix32(settings_.y_resolution);
info.ImageWidth  = FreeImage_GetWidth(current_fibitmap_);
info.ImageLength = FreeImage_GetHeight(current_fibitmap_);

bih.biXPelsPerMeter = static_cast<LONG>(settings_.x_resolution * 39.37);
bih.biYPelsPerMeter = static_cast<LONG>(settings_.y_resolution * 39.37);
```

### 3.8 File Transfer DPI reporting

`saveImageToFile()` writes the FreeImage-level metadata via `applyDpiMetadata`, calls `FreeImage_Save`, then `patchSavedDpiMetadata(path, x_dpi, y_dpi)` finalizes:

- PNG `pHYs`
- JFIF APP0 density
- BMP `biXPelsPerMeter` / `biYPelsPerMeter`
- TIFF `XResolution` / `YResolution` / `ResolutionUnit`

All sourced from `ScannerSettings.x_resolution` / `y_resolution`.

## 4. Key decisions and rationale

### 4.1 Real pixel resampling, not metadata-only DPI

- Decision: `applyPageSizeScaling` calls `FreeImage_Rescale` to `(page_w × DPI, page_h × DPI)`.
- Rationale: Real scanning applications (XnView "Scan to PDF", NAPS2, OCR middleware) trust `TW_IMAGEINFO.ImageWidth / Length` and the actual pixel count. Metadata-only DPI would silently break layout, OCR, and downstream PDF page size.

### 4.2 Fixed enum {150, 200, 300, 600}

- Decision: ENUMERATION instead of RANGE.
- Rationale: Bounded test matrix, simple dropdown, and protects against pathological values like 4800 DPI A4 (≈ 39680 × 56123 pixels). Enum can grow if needed.

### 4.3 `FILTER_LANCZOS3` as the default filter

- Decision: All rescales use `FILTER_LANCZOS3`.
- Rationale: Best built-in quality for mixed text / photo content. `BILINEAR` looks blurry, `BICUBIC` slightly softens text. Per-scan cost is negligible for one image at a time.

### 4.4 Resample before pixel-format conversion

- Decision: Rescale in 24-bit BGR, then convert to BW / GRAY.
- Rationale: Quantizing first destroys information that resampling would smear; doing conversion on the final resolution gives sharper thresholds and cleaner grayscale.

### 4.5 VirtualScanner owns all pixel work; DS only packages

- Decision: `TwainDataSource` calls `VirtualScanner::preScanPrep(settings_)` and consumes `current_fibitmap_`.
- Rationale: Single-responsibility. Both Native and File Transfer share one final FIBITMAP and cannot drift.

### 4.6 `round` instead of `floor` for `inch × DPI`

- Decision: `std::round`.
- Rationale: Keeps nominal page sizes consistent; floor would lose up to 1 pixel per dimension and back-compute to a slightly smaller page.

### 4.7 Three page_fill_mode policies

- Decision: Expose Stretch / Fit / Fill.
- Rationale: Different testing scenarios need different policies (OCR ≠ visual diff ≠ bleed-crop testing). All three are cheap to implement.

### 4.8 Resampling completed before strip transfer

- Decision: `preScanPrep` does all pixel work; `getScanStrip` only copies rows.
- Rationale: Native Transfer strip loop calls `getScanStrip` many times; per-call resampling would be slow and risk inconsistent intermediate states. Precomputing also lets `getImageInfo` report the true final dimensions immediately.

## 5. Architectural component changes

### 5.1 `src/capability.cpp`

- Register `ICAP_XRESOLUTION` / `ICAP_YRESOLUTION` as ENUMERATION, default 300, values `{150, 200, 300, 600}`.
- Validate `MSG_SET` against the enum, return `TWCC_BADVALUE` otherwise.
- Pair with `ICAP_UNITS = TWUN_INCHES` so semantics never shift.

### 5.2 `src/twain_data_source.cpp`

- `updateScannerFromCaps()` converts FIX32 → double for `ScannerSettings.x_resolution` / `y_resolution`.
- `handleDatImageInfo()` returns `XResolution` / `YResolution` from settings and `ImageWidth` / `ImageLength` from the prepared bitmap.
- `allocAndFillDibHeader()` fills `biXPelsPerMeter` / `biYPelsPerMeter` from settings (`DPI × 39.37`).
- `enableDs()` invokes `updateScannerFromCaps()` and `virtual_scanner_.preScanPrep(settings_)` after the UI submits.

### 5.3 `src/virtual_scanner.h/.cpp`

- Add `applyPageSizeScaling(const ScannerSettings&)`.
- Add `applyDpiMetadata(const ScannerSettings&)`.
- Refactor `preScanPrep`: `acquireImage → ensure24BitDib → applyPageSizeScaling → applyPixelFormat → applyDpiMetadata`.
- `saveImageToFile()` no longer rescales; it only saves and patches metadata.

### 5.4 `src/settings_server.cpp`

- DPI dropdown 150 / 200 / 300 / 600, default 300, selected from caps.
- On submit, write both X and Y resolution caps.
- i18n label for the DPI control.

### 5.5 Test impact

- Coverage matrix: 4 DPIs × 3 pixel types × 2 transfer modes × ≥2 page sizes.
- Output files validated to satisfy `pixel_w == round(page_w × DPI)` and `pixel_h == round(page_h × DPI)`, plus DPI metadata equality.

## 6. Limitations

- Only 150 / 200 / 300 / 600 are accepted; other values fail with `TWCC_BADVALUE`.
- Upscaling from low-resolution sources visibly softens; supply high-resolution images in `images/` for best results.
- `FILTER_LANCZOS3` at A3@600 DPI noticeably hits CPU and memory; no parallelization yet.
- Page size is limited to a few standard sheets plus Custom; `ICAP_FRAMES` (arbitrary crop frames in inches) is not implemented.
- The UI exposes one DPI dropdown only; asymmetric X / Y DPI is data-model-supported but not surfaced in the UI.
- A DPI change requires re-entering the scan flow; the source image is not re-loaded between strips.
- Extreme combinations (e.g. Custom 0.5×0.5" @ 150 DPI = 75×75 px) produce very small images; no minimum size guard.

## 7. Next steps

- Extend the DPI enum to `{75, 100, 150, 200, 300, 400, 600, 1200}` or switch to RANGE for stress tests.
- Cache `current_fibitmap_` keyed by `(source_path, page_size, x_dpi, y_dpi, fill_mode)`.
- Add automated post-scan validation: a Python tool reads each saved file and asserts pixel dimensions and DPI tags.
- Implement `ICAP_FRAMES` so applications can request custom inch-coordinate crops.
- Expose asymmetric X / Y DPI behind an "advanced" toggle.
- Investigate faster rescalers (multi-threaded Lanczos, or fallback to Bicubic for small scale ratios).
- Add multi-page consistency tests for future ADF / batch scenarios.

</details>
