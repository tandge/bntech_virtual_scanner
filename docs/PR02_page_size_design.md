# Page Size and Page Fill Support Design

Design notes for adding selectable page-size output and page-fill behavior to the BN Tech Virtual Scanner settings UI.

<details>
<summary>中文说明 / English</summary>

## 页面尺寸支持设计

## 1. 需求

settings UI 需要新增一个页面尺寸下拉框。选项包括 US Letter、US Legal、A4、A5。

页面尺寸下方还需要新增一个页面填充方式下拉框。选项包括 Stretch、Fit with padding、Fill and crop，默认值为 Stretch。

用户选择页面尺寸、DPI 和页面填充方式后，虚拟扫描仪输出图像的最终像素尺寸应按以下规则计算：

    像素宽度 = 页面宽度英寸值 * 水平 DPI
    像素高度 = 页面高度英寸值 * 垂直 DPI

示例：

    US Letter = 8.5 x 11 英寸
    DPI       = 600
    输出      = 5100 x 6600 像素

输出图像还必须保留匹配的 DPI 元数据，这样 Windows 资源管理器和图像应用才能显示用户选择的水平分辨率和垂直分辨率。

页面填充方式只影响源图像如何映射到目标页面画布，不改变最终输出页面像素尺寸。

## 2. 领域知识

### 2.1 TWAIN 传输路径

TWAIN Data Source 支持两个相关的输出路径。

1. Native Transfer
   - DS 在内存中返回 DIB 图像。
   - 应用程序，例如 XnView，可能会自己保存最终文件。
   - DPI 必须通过 TW_IMAGEINFO 和 DIB 元数据传递给应用。

2. File Transfer
   - DS 直接写出最终文件。
   - DS 可以在保存后修补不同文件格式自己的 DPI 元数据。

页面尺寸必须在任意一种传输路径返回图像数据之前应用。

### 2.2 DPI 的含义

本项目中的 DPI 表示 pixels per inch，即每英寸像素数。

TWAIN 分辨率值通过以下能力暴露：

    ICAP_XRESOLUTION
    ICAP_YRESOLUTION
    ICAP_UNITS = TWUN_INCHES

ICAP_UNITS = TWUN_INCHES 表示 TWAIN 应用应该把分辨率值解释为每英寸像素数。

### 2.3 页面尺寸

UI 使用常见页面尺寸。内部统一换算为英寸。

| 页面尺寸 | 物理尺寸 | 扫描器内部使用的英寸值 |
|---|---:|---:|
| US Letter | 8.5 x 11 in | 8.5 x 11.0 |
| US Legal | 8.5 x 14 in | 8.5 x 14.0 |
| A4 | 210 x 297 mm | 8.2677 x 11.6929 |
| A5 | 148 x 210 mm | 5.8268 x 8.2677 |

A 系列纸张尺寸会换算为英寸，并保留四位小数。

### 2.4 不同文件格式的 DPI 元数据

不同图像文件格式保存物理分辨率的方式不同。

| 格式 | 元数据字段 | 内部单位 |
|---|---|---|
| PNG | pHYs chunk | pixels per meter |
| JPG | JFIF APP0 density | dots per inch |
| BMP | biXPelsPerMeter 和 biYPelsPerMeter | pixels per meter |
| TIFF | XResolution、YResolution、ResolutionUnit | 有理数 + 英寸单位 |

PNG 和 BMP 内部使用 pixels per meter，因此扫描器需要把 DPI 转换为 pixels per meter。

### 2.5 页面填充方式

页面填充方式描述源图像宽高比与目标页面宽高比不一致时如何处理。

| 填充方式 | 是否保持源图像宽高比 | 输出页面是否完整覆盖 | 是否可能留白 | 是否可能裁剪 |
|---|---:|---:|---:|---:|
| Stretch | 否 | 是 | 否 | 否 |
| Fit with padding | 是 | 是 | 是 | 否 |
| Fill and crop | 是 | 是 | 否 | 是 |

- Stretch：把源图像直接缩放到目标页面宽高，可能改变源图像宽高比。
- Fit with padding：按较小缩放比例缩放源图像，使整张源图像完整放入页面；剩余区域使用白色背景填充。
- Fill and crop：按较大缩放比例缩放源图像，使页面被完全覆盖；超出页面的区域从中心裁剪。

页面填充方式是图像内容映射策略，不是 TWAIN 分辨率或文件 DPI 元数据策略。无论选择哪种填充方式，最终输出 bitmap 的宽高仍然等于页面尺寸乘以 DPI。

## 3. 设计目标

- 尽量减少代码改动。
- 复用现有 settings UI。
- 复用 ScannerSettings 传递页面尺寸和页面填充方式。
- 复用 FreeImage_Rescale 进行缩放。
- 使用 FreeImage_Allocate、FreeImage_FillBackground、FreeImage_Paste 实现留白画布。
- 使用 FreeImage_Copy 实现居中裁剪。
- 保持已有 DPI 元数据修复逻辑不变。
- 暂不新增 TWAIN 页面尺寸能力。

非目标：

- 不添加方向选择器。
- 不添加手动裁剪框 UI。
- 不添加背景色选择，Fit with padding 固定使用白色背景。
- 暂不支持 ICAP_SUPPORTEDSIZES。
- 暂不持久化页面尺寸。

## 4. 工作流程

    应用请求扫描
      -> 如果 ShowUI 为 true，DS 打开 settings UI
      -> 用户选择 DPI、页面尺寸和页面填充方式
      -> UI 把值提交给 DS
      -> DS 把值复制到 ScannerSettings
      -> VirtualScanner 加载下一张源图像
      -> VirtualScanner 按页面尺寸乘以 DPI 计算目标画布
      -> VirtualScanner 按页面填充方式缩放、留白或裁剪图像
      -> VirtualScanner 应用像素类型转换
      -> VirtualScanner 应用 DPI 元数据
      -> DS 返回 Native Transfer 图像，或者保存 File Transfer 输出文件

## 5. 算法

### 5.1 页面尺寸索引

选中的页面尺寸保存为整数。

    0 = US Letter
    1 = US Legal
    2 = A4
    3 = A5

如果收到非法值，则回退到 US Letter。

### 5.2 页面尺寸查表

扫描器把页面尺寸索引映射为宽度和高度，单位为英寸。

    0 -> 8.5,    11.0
    1 -> 8.5,    14.0
    2 -> 8.2677, 11.6929
    3 -> 5.8268, 8.2677

### 5.3 像素尺寸计算

已知 page_width_in、page_height_in、x_dpi、y_dpi：

    target_width  = round(page_width_in  * x_dpi)
    target_height = round(page_height_in * y_dpi)

当前实现使用“加 0.5 后转 int”的方式进行四舍五入。

### 5.4 页面填充方式索引

选中的页面填充方式保存为整数。

    0 = Stretch
    1 = Fit with padding
    2 = Fill and crop

如果收到非法值，则回退到 Stretch。

### 5.5 图像缩放和填充算法

所有模式都会先计算目标页面像素尺寸 target_width 和 target_height。不同填充方式只影响源图像内容如何映射到这个目标页面。

#### 5.5.1 Stretch

Stretch 直接把源图像缩放到目标页面尺寸：

    output_width  = target_width
    output_height = target_height

使用 FreeImage_Rescale 和 FILTER_BILINEAR。该模式不会留白，也不会裁剪，但可能改变源图像宽高比。

#### 5.5.2 Fit with padding

Fit with padding 保持源图像宽高比，并确保整张源图像可见。

已知源图像尺寸 src_width、src_height：

    scale_x = target_width  / src_width
    scale_y = target_height / src_height
    scale   = min(scale_x, scale_y)

缩放后尺寸：

    scaled_width  = round(src_width  * scale)
    scaled_height = round(src_height * scale)

然后创建目标页面大小的白色画布：

    canvas_width  = target_width
    canvas_height = target_height

把缩放后的图像居中粘贴到画布：

    left = (target_width  - scaled_width)  / 2
    top  = (target_height - scaled_height) / 2

实现使用：

- FreeImage_Rescale 缩放源图像。
- FreeImage_Allocate 创建目标画布。
- FreeImage_FillBackground 填充白色背景。
- FreeImage_Paste 把缩放图像粘贴到画布中心。

#### 5.5.3 Fill and crop

Fill and crop 保持源图像宽高比，并确保目标页面被图像完全覆盖。

    scale_x = target_width  / src_width
    scale_y = target_height / src_height
    scale   = max(scale_x, scale_y)

缩放后尺寸：

    scaled_width  = ceil(src_width  * scale)
    scaled_height = ceil(src_height * scale)

然后从缩放后的图像中心裁剪目标页面大小的区域：

    left = (scaled_width  - target_width)  / 2
    top  = (scaled_height - target_height) / 2
    crop = [left, top, left + target_width, top + target_height]

实现使用：

- FreeImage_Rescale 缩放源图像。
- FreeImage_Copy 从中心裁剪目标页面区域。

### 5.6 元数据

缩放完成后，applyDpiMetadata 会把 DPI 写入内存 bitmap。对于 File Transfer，保存文件后 patchSavedDpiMetadata 仍会继续修补 PNG、JPG、BMP、TIFF 的容器级元数据。

## 6. 示例

### 6.1 页面像素尺寸示例

| 页面尺寸 | 300 DPI | 600 DPI |
|---|---:|---:|
| US Letter | 2550 x 3300 | 5100 x 6600 |
| US Legal | 2550 x 4200 | 5100 x 8400 |
| A4 | 2480 x 3508 | 4961 x 7016 |
| A5 | 1748 x 2480 | 3496 x 4961 |

### 6.2 页面填充方式示例

假设源图像为 1600 x 900，目标页面为 US Letter 300 DPI，即 2550 x 3300。

| 填充方式 | 缩放后内容尺寸 | 最终输出尺寸 | 结果 |
|---|---:|---:|---|
| Stretch | 2550 x 3300 | 2550 x 3300 | 图像被拉伸为页面比例 |
| Fit with padding | 2550 x 1434 | 2550 x 3300 | 上下留白，源图完整可见 |
| Fill and crop | 5867 x 3300 | 2550 x 3300 | 左右裁剪，页面完全填满 |

## 7. 分层改动

### 7.1 Settings UI 层

文件：

    src/settings_server.h
    src/settings_server.cpp

改动：

- 在 SettingsUiResult 中添加 page_size 和 page_fill_mode。
- 在生成的 HTML 中添加 Page Size 下拉框。
- 在 Page Size 下方添加 Page Fill 下拉框。
- 扫描提交请求中携带 pagesize 和 pagefillmode。
- 在 parseFormData 中解析 pagesize 和 pagefillmode。

### 7.2 TWAIN Data Source 层

文件：

    src/twain_data_source.cpp

改动：

- 显示 UI 前，从 scanner_.getSettings().page_size 初始化 ui_result.page_size。
- 显示 UI 前，从 scanner_.getSettings().page_fill_mode 初始化 ui_result.page_fill_mode。
- 用户点击 Scan 后，把 ui_result.page_size 和 ui_result.page_fill_mode 复制到 ScannerSettings。
- 现有 DPI 传递逻辑保持不变。

### 7.3 Scanner Settings 层

文件：

    src/virtual_scanner.h

改动：

    ScannerSettings 现在包含 page_size 和 page_fill_mode。

默认页面尺寸为 US Letter。默认页面填充方式为 Stretch。

### 7.4 图像处理层

文件：

    src/virtual_scanner.cpp

改动：

- resetScanner 把 settings_.page_size 设置为 0。
- resetScanner 把 settings_.page_fill_mode 设置为 0。
- preScanPrep 调用 applyPageSizeScaling。
- applyPageSizeScaling 根据页面尺寸和 DPI 计算输出像素尺寸。
- applyPageSizeScaling 根据页面填充方式执行 Stretch、Fit with padding 或 Fill and crop。
- FreeImage_Rescale 执行实际缩放。
- FreeImage_Allocate、FreeImage_FillBackground、FreeImage_Paste 执行留白画布处理。
- FreeImage_Copy 执行居中裁剪。

处理顺序：

    加载图像
      -> 确保是 24-bit DIB
      -> 按选中页面尺寸和 DPI 计算目标页面
      -> 按页面填充方式缩放、留白或裁剪
      -> 转换像素类型
      -> 应用 DPI 元数据
      -> 计算行参数

### 7.5 元数据层

不需要新增主要的元数据算法。已有逻辑仍然处理：

- TW_IMAGEINFO 的 XResolution 和 YResolution。
- PNG pHYs。
- JPG JFIF density。
- BMP DIB 分辨率字段。
- TIFF 分辨率标签。

## 8. 端到端数据流

    TWAIN 应用
      -> TwainDataSource MSG_ENABLEDS
      -> SettingsServer showSettingsUi
      -> 用户选择 DPI、页面尺寸和页面填充方式
      -> TwainDataSource 把值保存到 ScannerSettings
      -> VirtualScanner acquireImage
      -> FreeImage 加载源图像
      -> VirtualScanner 按页面英寸值乘以 DPI 计算目标页面
      -> VirtualScanner 按填充方式执行缩放、留白或裁剪
      -> VirtualScanner 应用像素类型和 DPI 元数据
      -> TwainDataSource 发送 MSG_XFERREADY
      -> 应用请求 image info 和 image data
      -> 应用收到页面尺寸匹配的图像

## 9. 测试清单

Native Transfer，例如 XnView 的“扫描到”：

1. 打开 XnView。
2. 选择“扫描到”。
3. 选择输出格式。
4. 点击扫描。
5. 在 settings UI 中选择 DPI、Page Size 和 Page Fill。
6. 点击 Scan。
7. 验证像素宽高是否等于页面尺寸乘以 DPI。
8. 验证 Stretch 会拉伸到页面比例。
9. 验证 Fit with padding 会保持比例并使用白色留白。
10. 验证 Fill and crop 会保持比例并居中裁剪。
11. 验证 Windows 详细信息页是否显示所选 DPI。

File Transfer：

- 测试 PNG、JPG、BMP、TIFF。
- 验证像素尺寸。
- 验证 DPI 元数据。

## 10. 当前限制和未来改进

当前限制：

- Fit with padding 的背景色固定为白色，不能在 UI 中修改。
- Fill and crop 固定为居中裁剪，不能在 UI 中选择裁剪锚点。
- 页面方向固定为纵向。
- 页面尺寸没有通过 ICAP_SUPPORTEDSIZES 暴露。
- 页面尺寸不会跨会话持久化。

未来改进：

- 添加纵向和横向方向选择。
- 允许用户选择 Fit with padding 的背景色。
- 允许用户选择 Fill and crop 的裁剪锚点。
- 添加 ICAP_SUPPORTEDSIZES 和 ICAP_FRAMES 支持。
- 持久化上次选择的页面尺寸和填充方式。
- 添加自动化测试，验证各格式的像素尺寸和 DPI 元数据。

</details>

## 1. Requirement

The settings UI adds a Page Size dropdown. Options are US Letter, US Legal, A4, and A5.

The UI also adds a Page Fill dropdown directly below Page Size. Options are Stretch, Fit with padding, and Fill and crop. The default is Stretch.

After the user selects page size, DPI, and page-fill behavior, final scan output pixel size is computed as:

    pixel_width  = page_width_in_inches  * horizontal_dpi
    pixel_height = page_height_in_inches * vertical_dpi

Example:

    US Letter = 8.5 x 11 inches
    DPI       = 600
    Output    = 5100 x 6600 pixels

The output must also keep matching DPI metadata, so Windows Explorer and image applications show the selected horizontal and vertical resolution.

The page-fill setting only controls how source-image content is mapped into the target page canvas. It does not change the final output pixel size.

## 2. Domain knowledge

### 2.1 TWAIN transfer paths

The TWAIN Data Source supports two relevant output paths.

1. Native Transfer
   - The DS returns a DIB image in memory.
   - The application, such as XnView, may save the final file itself.
   - DPI must be propagated through TW_IMAGEINFO and DIB metadata.

2. File Transfer
   - The DS writes the final file.
   - The DS can patch file format specific DPI metadata after saving.

Page size must be applied before either transfer path returns image data.

### 2.2 DPI meaning

DPI means pixels per inch. TWAIN resolution values use:

    ICAP_XRESOLUTION
    ICAP_YRESOLUTION
    ICAP_UNITS = TWUN_INCHES

ICAP_UNITS = TWUN_INCHES tells TWAIN applications that the resolution values are pixels per inch.

### 2.3 Page sizes

| Page size | Physical size | Inches used |
|---|---:|---:|
| US Letter | 8.5 x 11 in | 8.5 x 11.0 |
| US Legal | 8.5 x 14 in | 8.5 x 14.0 |
| A4 | 210 x 297 mm | 8.2677 x 11.6929 |
| A5 | 148 x 210 mm | 5.8268 x 8.2677 |

A-series sizes are converted to inches and rounded to four decimal places.

### 2.4 DPI metadata by file type

| Format | Metadata field | Internal unit |
|---|---|---|
| PNG | pHYs chunk | pixels per meter |
| JPG | JFIF APP0 density | dots per inch |
| BMP | biXPelsPerMeter and biYPelsPerMeter | pixels per meter |
| TIFF | XResolution, YResolution, ResolutionUnit | rational plus inch unit |

PNG and BMP use pixels per meter internally, so the scanner converts DPI to pixels per meter.

### 2.5 Page-fill behavior

Page-fill behavior describes how to handle aspect-ratio differences between the source image and the target page.

| Fill mode | Preserves source aspect ratio | Covers final page | May add padding | May crop |
|---|---:|---:|---:|---:|
| Stretch | No | Yes | No | No |
| Fit with padding | Yes | Yes | Yes | No |
| Fill and crop | Yes | Yes | No | Yes |

- Stretch: resize the source image directly to the target page size. This may distort the source aspect ratio.
- Fit with padding: use the smaller scale factor so the full source image fits inside the page, then fill the remaining canvas with white padding.
- Fill and crop: use the larger scale factor so the page is fully covered, then crop overflow around the centered page area.

Page-fill behavior is an image-content mapping strategy. It is not a TWAIN resolution or file DPI metadata strategy. For all fill modes, the final bitmap width and height still equal page size times DPI.

## 3. Design goals

- Keep code changes small.
- Reuse the existing settings UI.
- Reuse ScannerSettings to pass page size and page-fill behavior.
- Reuse FreeImage_Rescale for resizing.
- Use FreeImage_Allocate, FreeImage_FillBackground, and FreeImage_Paste for padded canvases.
- Use FreeImage_Copy for centered crop.
- Keep existing DPI metadata fixes unchanged.
- Do not add a new TWAIN page-size capability yet.

Non-goals:

- No orientation selector.
- No manual crop rectangle UI.
- No background color selector; Fit with padding always uses white.
- No ICAP_SUPPORTEDSIZES support yet.
- No persistence of page size yet.

## 4. Workflow

    Application requests scan
      -> DS opens settings UI when ShowUI is true
      -> User selects DPI, Page Size, and Page Fill
      -> UI submits values to DS
      -> DS copies values into ScannerSettings
      -> VirtualScanner loads next source image
      -> VirtualScanner computes target canvas from page size times DPI
      -> VirtualScanner scales, pads, or crops according to Page Fill
      -> VirtualScanner applies pixel type conversion
      -> VirtualScanner applies DPI metadata
      -> DS returns Native Transfer image or saves File Transfer output

## 5. Algorithm

### 5.1 Page size index

The selected page size is stored as an integer.

    0 = US Letter
    1 = US Legal
    2 = A4
    3 = A5

Invalid values fall back to US Letter.

### 5.2 Page size lookup

The scanner maps the index to width and height in inches.

    0 -> 8.5,    11.0
    1 -> 8.5,    14.0
    2 -> 8.2677, 11.6929
    3 -> 5.8268, 8.2677

### 5.3 Pixel calculation

Given page_width_in, page_height_in, x_dpi, and y_dpi:

    target_width  = round(page_width_in  * x_dpi)
    target_height = round(page_height_in * y_dpi)

The implementation uses add 0.5 then cast to int for rounding.

### 5.4 Page-fill mode index

The selected page-fill behavior is stored as an integer.

    0 = Stretch
    1 = Fit with padding
    2 = Fill and crop

Invalid values fall back to Stretch.

### 5.5 Scaling and fill algorithm

All modes first compute the target page pixel size: target_width and target_height. The fill mode only controls how source pixels map into that target page.

#### 5.5.1 Stretch

Stretch directly resizes the source image to the target page dimensions:

    output_width  = target_width
    output_height = target_height

It uses FreeImage_Rescale with FILTER_BILINEAR. This mode adds no padding and crops nothing, but it may distort the source aspect ratio.

#### 5.5.2 Fit with padding

Fit with padding preserves source aspect ratio and keeps the full source image visible.

Given source dimensions src_width and src_height:

    scale_x = target_width  / src_width
    scale_y = target_height / src_height
    scale   = min(scale_x, scale_y)

Scaled content size:

    scaled_width  = round(src_width  * scale)
    scaled_height = round(src_height * scale)

Then create a white target-page canvas:

    canvas_width  = target_width
    canvas_height = target_height

Paste the scaled source image centered on the canvas:

    left = (target_width  - scaled_width)  / 2
    top  = (target_height - scaled_height) / 2

Implementation uses:

- FreeImage_Rescale to scale the source image.
- FreeImage_Allocate to create the target canvas.
- FreeImage_FillBackground to fill the canvas with white.
- FreeImage_Paste to paste the scaled image centered in the canvas.

#### 5.5.3 Fill and crop

Fill and crop preserves source aspect ratio and ensures the target page is fully covered.

    scale_x = target_width  / src_width
    scale_y = target_height / src_height
    scale   = max(scale_x, scale_y)

Scaled content size:

    scaled_width  = ceil(src_width  * scale)
    scaled_height = ceil(src_height * scale)

Then crop the target page area from the center of the scaled image:

    left = (scaled_width  - target_width)  / 2
    top  = (scaled_height - target_height) / 2
    crop = [left, top, left + target_width, top + target_height]

Implementation uses:

- FreeImage_Rescale to scale the source image.
- FreeImage_Copy to crop the centered target page area.

### 5.6 Metadata

After scaling, applyDpiMetadata writes DPI to the in-memory bitmap. For file transfer, patchSavedDpiMetadata still patches PNG, JPG, BMP, and TIFF container metadata after saving.

## 6. Examples

### 6.1 Page pixel-size examples

| Page size | 300 DPI | 600 DPI |
|---|---:|---:|
| US Letter | 2550 x 3300 | 5100 x 6600 |
| US Legal | 2550 x 4200 | 5100 x 8400 |
| A4 | 2480 x 3508 | 4961 x 7016 |
| A5 | 1748 x 2480 | 3496 x 4961 |

### 6.2 Page-fill examples

Assume the source image is 1600 x 900 and the target page is US Letter at 300 DPI, so the target is 2550 x 3300.

| Fill mode | Scaled content size | Final output size | Result |
|---|---:|---:|---|
| Stretch | 2550 x 3300 | 2550 x 3300 | Image is distorted to page aspect ratio |
| Fit with padding | 2550 x 1434 | 2550 x 3300 | Top/bottom padding, full source visible |
| Fill and crop | 5867 x 3300 | 2550 x 3300 | Left/right crop, page fully covered |

## 7. Layer changes

### 7.1 Settings UI layer

Files:

    src/settings_server.h
    src/settings_server.cpp

Changes:

- Add page_size and page_fill_mode to SettingsUiResult.
- Add Page Size dropdown to generated HTML.
- Add Page Fill dropdown below Page Size.
- Submit pagesize and pagefillmode with the scan request.
- Parse pagesize and pagefillmode in parseFormData.

### 7.2 TWAIN data source layer

File:

    src/twain_data_source.cpp

Changes:

- Initialize ui_result.page_size from scanner_.getSettings().page_size.
- Initialize ui_result.page_fill_mode from scanner_.getSettings().page_fill_mode.
- Copy ui_result.page_size and ui_result.page_fill_mode into ScannerSettings after the user clicks Scan.
- Existing DPI propagation remains unchanged.

### 7.3 Scanner settings layer

File:

    src/virtual_scanner.h

Change:

    ScannerSettings now includes page_size and page_fill_mode.

Default page size is US Letter. Default page-fill mode is Stretch.

### 7.4 Image processing layer

File:

    src/virtual_scanner.cpp

Changes:

- resetScanner sets settings_.page_size to 0.
- resetScanner sets settings_.page_fill_mode to 0.
- preScanPrep calls applyPageSizeScaling.
- applyPageSizeScaling computes output pixels from page size and DPI.
- applyPageSizeScaling applies Stretch, Fit with padding, or Fill and crop according to the selected page-fill mode.
- FreeImage_Rescale performs the actual resize.
- FreeImage_Allocate, FreeImage_FillBackground, and FreeImage_Paste perform padded-canvas handling.
- FreeImage_Copy performs centered cropping.

Processing order:

    Load image
      -> Ensure 24-bit DIB
      -> Compute target page from selected page size and DPI
      -> Scale, pad, or crop according to selected page-fill mode
      -> Convert pixel type
      -> Apply DPI metadata
      -> Calculate row parameters

### 7.5 Metadata layer

No major new metadata algorithm is needed. Existing logic still handles:

- TW_IMAGEINFO XResolution and YResolution.
- PNG pHYs.
- JPG JFIF density.
- BMP DIB resolution fields.
- TIFF resolution tags.

## 8. End-to-end data flow

    TWAIN application
      -> TwainDataSource MSG_ENABLEDS
      -> SettingsServer showSettingsUi
      -> user selects DPI, page size, and page-fill mode
      -> TwainDataSource stores values in ScannerSettings
      -> VirtualScanner acquireImage
      -> FreeImage loads source image
      -> VirtualScanner computes target page from page inches times DPI
      -> VirtualScanner scales, pads, or crops according to fill mode
      -> VirtualScanner applies pixel type and DPI metadata
      -> TwainDataSource sends MSG_XFERREADY
      -> Application requests image info and image data
      -> Application receives page-sized image

## 9. Testing checklist

Native transfer, for example XnView Scan to:

1. Open XnView.
2. Choose Scan to.
3. Select output format.
4. Click Scan.
5. In settings UI, choose DPI, Page Size, and Page Fill.
6. Click Scan.
7. Verify pixel width and height match page size times DPI.
8. Verify Stretch distorts the image to the page aspect ratio.
9. Verify Fit with padding preserves aspect ratio and uses white padding.
10. Verify Fill and crop preserves aspect ratio and center-crops overflow.
11. Verify Windows Details tab shows selected DPI.

File transfer:

- Test PNG, JPG, BMP, and TIFF.
- Verify pixel dimensions.
- Verify DPI metadata.

## 10. Limitations and future improvements

Current limitations:

- Fit with padding uses a fixed white background; the UI cannot change it.
- Fill and crop always uses centered cropping; the UI cannot select a crop anchor.
- Page orientation is portrait only.
- Page size is not exposed via ICAP_SUPPORTEDSIZES.
- Page size is not persisted across sessions.

Future improvements:

- Add portrait and landscape orientation.
- Allow users to choose Fit with padding background color.
- Allow users to choose Fill and crop anchor point.
- Add ICAP_SUPPORTEDSIZES and ICAP_FRAMES support.
- Persist last selected page size and fill mode.
- Add automated tests for dimensions and DPI metadata.
