# GIF / WebP Output Design

Design notes for adding GIF and WebP as selectable output formats in the BN Tech Virtual Scanner settings UI and file-output pipeline.

<details open>
<summary>中文说明</summary>

## 1. 需求

在 settings UI 的输出文件格式下拉框中新增两种格式：

- `WEBP`
- `GIF`

用户选择 **File Transfer** 输出模式时，可以选择 WebP 或 GIF，并生成对应扩展名和编码格式的输出文件。

主要需求：

- 在现有 `PNG / JPG / BMP / TIFF` 基础上增加 `WEBP / GIF`。
- 输出文件名后缀预览同步支持 `.webp` 和 `.gif`。
- `VirtualScanner::saveImageToFile()` 根据 UI 选择写出 WebP/GIF。
- `VirtualScanner::saveImageToPath()` 根据 `.webp` / `.gif` 扩展名写出对应格式。
- 改动尽量少，继续复用现有 FreeImage 依赖，不引入新图像库。

非目标：

- 不增加 WebP/GIF 独立质量参数 UI。
- 不支持动画 GIF 或动画 WebP。
- 不把 WebP/GIF 伪装成 TWAIN 标准文件格式，因为当前 TWAIN 头文件没有 `TWFF_WEBP` / `TWFF_GIF`。

## 2. 相关领域知识

### 2.1 FreeImage 输出格式

项目已经使用 FreeImage 负责图片加载、转换和保存。当前 FreeImage 头文件中已有：

```c
FIF_WEBP
FIF_GIF
```

因此新增 WebP/GIF 输出不需要引入额外依赖，只需要在保存时选择对应 `FREE_IMAGE_FORMAT`，并继续调用：

```c
FreeImage_Save(fif, dib, path, flags)
```

### 2.2 WebP

WebP 是现代图片格式，通常用于较小文件体积。它支持有损、无损和透明通道，但当前实现仅使用 FreeImage 的静态图片保存能力。

本次实现中，WebP 复用 JPEG 的固定质量参数：

```c
flags = 85
```

这样可以保持现有风格：不新增 UI，不新增配置项，用一个合理默认值完成输出。

### 2.3 GIF

GIF 是 8-bit 调色板格式，单帧最多 256 色。扫描器内部图像通常是 24-bit RGB/BGR，如果直接保存为 GIF，部分 FreeImage 版本可能失败或效果不可控。

因此保存 GIF 前会临时量化：

```c
FreeImage_ColorQuantize(dib_, FIQ_WUQUANT)
```

该操作生成临时 8-bit 调色板图，只用于 `FreeImage_Save(FIF_GIF, ...)`：

- 不修改扫描器内部原始 `dib_`。
- 不影响 Native Transfer 或后续扫描线输出。
- 保存后释放临时图，避免内存泄漏。

### 2.4 TWAIN 文件格式能力限制

TWAIN 标准 `ICAP_IMAGEFILEFORMAT` 常见值包括 TIFF、BMP、JFIF、PNG、PDF 等。本项目使用的 `twain.h` 中没有 WebP/GIF 对应常量。

因此当前设计把两层概念分开：

- UI 内部格式索引：`4 = WEBP`，`5 = GIF`。
- FreeImage 实际保存格式：`FIF_WEBP` / `FIF_GIF`。
- TWAIN capability 层：WebP/GIF 回退映射为 `TWFF_PNG`，避免暴露非标准值。

## 3. 设计决策和原因

### 3.1 沿用整数格式索引

**决策**：继续使用现有 `file_format` 整数索引，并追加两个值。

| 值 | 格式 | 扩展名 | FreeImage 格式 |
|---:|---|---|---|
| 0 | PNG | `.png` | `FIF_PNG` |
| 1 | JPG | `.jpg` | `FIF_JPEG` |
| 2 | BMP | `.bmp` | `FIF_BMP` |
| 3 | TIFF | `.tif` | `FIF_TIFF` |
| 4 | WEBP | `.webp` | `FIF_WEBP` |
| 5 | GIF | `.gif` | `FIF_GIF` |

**原因**：

- 现有 UI、表单提交和保存逻辑已经按索引映射格式。
- 只扩展数组即可完成主体功能，改动最少。
- 避免重构设置结构或新增枚举类型。

### 3.2 WebP 使用质量 85

**决策**：WebP 与 JPEG 一样使用质量参数 `85`。

**原因**：

- 当前 JPEG 已经固定使用 `85`。
- WebP 质量 85 通常能在画质和体积之间取得较好平衡。
- 不新增质量配置，符合本次“小改动”目标。

### 3.3 GIF 保存前临时量化

**决策**：保存 GIF 且当前 DIB 位深大于 8 bit 时，先调用 `FreeImage_ColorQuantize(dib_, FIQ_WUQUANT)`。

**原因**：

- GIF 格式本身只能保存调色板图像。
- FreeImage 内置量化能避免自行处理调色板、位深和行对齐。
- 临时量化不污染 `dib_`，不会影响其他输出路径。

### 3.4 不修改 TWAIN 标准头文件

**决策**：不新增 `TWFF_WEBP` / `TWFF_GIF`，不改 `twain.h`。

**原因**：

- TWAIN 标准没有这两个文件格式值。
- 修改标准头文件会制造兼容性和维护风险。
- 外部宿主可能不理解非标准格式值。

### 3.5 WebP/GIF 在 TWAIN capability 层回退为 PNG

**决策**：WebP/GIF 对 `ICAP_IMAGEFILEFORMAT` 使用 `TWFF_PNG` 回退。

**原因**：

- PNG 是当前默认格式，兼容性最好。
- 避免向 TWAIN 宿主返回非标准值。
- 实际文件保存仍由 `scanner_.setOutputFormat(ui_result.file_format)` 传入的内部索引决定。

这是兼容性折中：settings UI 场景可以正确输出 WebP/GIF，但 TWAIN capability 查询无法准确表达这两个格式。

### 3.6 `saveImageToPath()` 支持扩展名推断

**决策**：在扩展名推断逻辑中新增：

```cpp
else if (ext == "webp") fif = FIF_WEBP;
else if (ext == "gif") fif = FIF_GIF;
```

**原因**：

- 项目已有从路径扩展名推断输出格式的逻辑。
- 追加两个分支即可支持外部路径输出 WebP/GIF。
- 保持 `saveImageToFile()` 和 `saveImageToPath()` 行为一致。

## 4. 架构各层次的改动

### 4.1 Settings UI 层：`settings_server.cpp`

新增两个下拉框选项：

```html
<option value='4'>WEBP</option>
<option value='5'>GIF</option>
```

更新前端扩展名数组：

```js
var EXTS=['.png','.jpg','.bmp','.tif','.webp','.gif'];
```

作用：

- 用户可选择 WebP/GIF。
- 输出文件名旁显示正确后缀。
- 继续通过现有 `fileformat` 参数提交，无需新增表单字段。

### 4.2 Settings 数据结构层：`settings_server.h`

更新 `SettingsUiResult::file_format` 注释：

```cpp
int file_format; // 0=PNG, 1=JPG, 2=BMP, 3=TIFF, 4=WEBP, 5=GIF
```

该改动只更新说明，不改变结构布局。

### 4.3 扫描器文件输出层：`virtual_scanner.cpp`

`saveImageToFile()` 扩展格式数组：

```cpp
static const FREE_IMAGE_FORMAT kFiFmts[] = {
  FIF_PNG, FIF_JPEG, FIF_BMP, FIF_TIFF, FIF_WEBP, FIF_GIF
};
static const char* kExts[] = {
  ".png", ".jpg", ".bmp", ".tif", ".webp", ".gif"
};
```

保存 flags：

```cpp
int flags = (fif == FIF_JPEG || fif == FIF_WEBP) ? 85 : 0;
```

GIF 临时量化：

```cpp
FIBITMAP* save_dib = (fif == FIF_GIF && FreeImage_GetBPP(dib_) > 8)
    ? FreeImage_ColorQuantize(dib_, FIQ_WUQUANT) : dib_;
bool saved = save_dib && FreeImage_Save(fif, save_dib, path, flags) != FALSE;
if (save_dib != dib_) FreeImage_Unload(save_dib);
```

`saveImageToPath()` 额外支持 `.webp` 和 `.gif` 扩展名，并复用相同保存逻辑。

### 4.4 TWAIN 数据源层：`twain_data_source.cpp`

扩展 UI 格式索引到 TWAIN 标准格式的映射：

```cpp
static const int kTwffMap[] = {
  TWFF_PNG, TWFF_JFIF, TWFF_BMP, TWFF_TIFF, TWFF_PNG, TWFF_PNG
};
```

含义：

- `0..3` 保持原有 PNG/JPEG/BMP/TIFF 映射。
- `4=WEBP` 和 `5=GIF` 在 TWAIN capability 中回退为 `TWFF_PNG`。
- 实际输出格式仍通过 `scanner_.setOutputFormat(ui_result.file_format)` 写入扫描器内部状态。

`DAT_SETUPFILEXFER` 中也识别 `.webp` / `.gif` 扩展名，并在 capability 层回退为 PNG：

```cpp
else if (ext == "webp" || ext == "gif") ff = TWFF_PNG;
```

### 4.5 改动文件清单

```text
src/settings_server.cpp
src/settings_server.h
src/virtual_scanner.cpp
src/twain_data_source.cpp
docs/gif_webp_design.md
```

## 5. 局限性和可能风险

### 5.1 TWAIN capability 不能准确表达 WebP/GIF

由于标准没有 `TWFF_WEBP` / `TWFF_GIF`，外部宿主通过 `ICAP_IMAGEFILEFORMAT` 查询时无法看到真实支持 WebP/GIF。当前只保证 settings UI 文件输出路径能正确写出这两种格式。

### 5.2 `DAT_SETUPFILEXFER` 场景存在语义不一致

外部应用如果传入 `.webp` 或 `.gif` 路径：

- `saveImageToPath()` 可以按扩展名实际写出 WebP/GIF。
- capability 中记录的格式是 `TWFF_PNG` 回退值。

如果宿主同时检查 `data->Format` 和文件扩展名，可能看到二者不一致。

### 5.3 GIF 会损失颜色

GIF 最多 256 色。彩色扫描件保存为 GIF 时会量化：

- 照片和渐变可能出现色带。
- 复杂彩色图像画质会下降。
- GIF 更适合黑白、灰度或简单色块图像。

### 5.4 不支持动画或多页 GIF/WebP

当前实现只保存单张静态图片：

- 不支持动画 GIF。
- 不支持动画 WebP。
- 不支持把多页扫描合并到一个 GIF/WebP 文件。

### 5.5 WebP 支持依赖 FreeImage DLL

虽然头文件声明了 `FIF_WEBP`，但实际能否写 WebP 取决于发布包中的 FreeImage DLL 是否启用 WebP 插件。需要在 win32/win64 实际包中验证。

### 5.6 缺少格式范围校验

`output_format_` 当前直接作为数组索引使用。正常 UI 只会提交 `0..5`，但如果表单被手工构造为非法值，可能导致越界访问。该问题是既有设计的延续，新增格式后更值得补充防御性校验。

### 5.7 DPI metadata 支持有限

项目已有 DPI metadata 写入和补丁逻辑，但 GIF/WebP 对 DPI metadata 的支持程度依赖 FreeImage 插件和外部查看器。是否能被 Windows Explorer 或其他软件识别，需要实际验证。

## 6. 下一步工作

1. **验证真实输出**
   - 使用 win32 和 win64 FreeImage DLL 分别测试 `.webp` / `.gif`。
   - 确认文件可被常见图片查看器打开。

2. **增加格式范围校验**
   - 在 `setOutputFormat()` 或 `saveImageToFile()` 中限制合法范围为 `0..5`。
   - 非法值回退到 PNG。

3. **增加 FreeImage 写能力检查**
   - 保存前调用 `FreeImage_FIFSupportsWriting(fif)`。
   - 不支持时返回失败并写日志。

4. **改善文档和用户提示**
   - 在用户指南中说明 GIF 的 256 色限制。
   - 说明 WebP/GIF 是 settings UI 内部输出能力，不是 TWAIN 标准 `ICAP_IMAGEFILEFORMAT` 能力。

5. **补充测试**
   - 回归 PNG/JPG/BMP/TIFF 输出。
   - 测试 WEBP 扩展名和实际编码。
   - 测试 GIF 8-bit 调色板和文件可打开性。
   - 测试 `saveImageToPath()` 对 `.webp` / `.gif` 的扩展名推断。

</details>
