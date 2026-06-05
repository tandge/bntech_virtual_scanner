# Rotation Design (0° / 90° / 180° / 270°)

Design notes for the rotation option in the BN Tech Virtual Scanner settings UI and image processing pipeline.

<details open>
<summary>中文说明</summary>

## 1. 需求

在 settings UI 的扫描设置区提供 **Rotation**（旋转）选项，允许用户在扫描输出前将图像按顺时针方向旋转 0°、90°、180° 或 270°，默认不旋转。

主要功能需求：

- settings UI 提供一个下拉列表框，位于 **Page Size** 配置项下方、**Flip** 配置项上方，包含四个选项：
  - `0 degree` — 不旋转（默认）
  - `90 degree` — 顺时针旋转 90°
  - `180 degree` — 顺时针旋转 180°
  - `270 degree` — 顺时针旋转 270°
- 用户选择的 rotation 值必须在 settings UI 关闭后正确回写到 `ScannerSettings`，并作用于下一张扫描图像。
- 旋转必须同时作用于 **Native Transfer** 返回的 DIB 和 **File Transfer** 写出的文件。
- 旋转必须与现有的 Page Size Scaling、Flip、Pixel Type 转换等功能正交组合：任意组合都应产生预期结果。
- 对于 90° 和 270° 旋转，输出图像的宽高必须互换，且 `TW_IMAGEINFO` 中上报的 `ImageWidth`、`ImageLength`、`XResolution`、`YResolution` 必须与实际输出一致。

非功能性需求：

- 改动范围尽量小，不引入新的外部依赖。
- 继续复用 FreeImage 完成图像几何变换。
- 支持现有中英双语本地化（英文：`0 degree` / `90 degree` / `180 degree` / `270 degree`；中文：`0 度` / `90 度` / `180 度` / `270 度`）。
- 默认值为 `0 degree`，老用户升级后行为不变。
- 90° 倍数的旋转必须是“无损”的：使用整数倍角度，避免亚像素插值带来的模糊。

## 2. 领域知识

### 2.1 图像旋转的分类

| 变换 | 几何意义 | 角度特点 | 常见实现 |
|---|---|---|---|
| 顺时针 90° 旋转 | 宽高互换，原左上角 → 右上角 | 90° 整数倍 | `FreeImage_Rotate(dib, -90.0, nullptr)` |
| 顺时针 180° 旋转 | 中心对称，四角对调 | 90° 整数倍 | `FreeImage_Rotate(dib, -180.0, nullptr)` |
| 顺时针 270° 旋转 | 宽高互换，原左上角 → 左下角 | 90° 整数倍 | `FreeImage_Rotate(dib, -270.0, nullptr)` |
| 任意角度旋转 | 需要插值，会引入模糊或锯齿 | 非 90° 倍数 | `FreeImage_Rotate(dib, angle, nullptr)` |

### 2.2 FreeImage 旋转 API

FreeImage 提供通用旋转函数：

```c
DLL_API FIBITMAP* DLL_CALLCONV FreeImage_Rotate(FIBITMAP *dib, double angle, const void *bkcolor);
```

特点：

- **非原地操作**：返回一个新的 `FIBITMAP*`，调用方需要 `FreeImage_Unload` 旧句柄并接管新句柄。
- **角度方向**：正角度表示 **逆时针**，负角度表示 **顺时针**。因此顺时针 90° 需要传入 `-90.0`。
- **90° 整数倍优化**：FreeImage 内部对 90° 整数倍使用特殊路径，不经过双线性/双三次插值，可以做到像素级精确。
- **背景色**：`bkcolor` 为 `nullptr` 时，旋转后露出的背景由 FreeImage 默认填充。对于 90° 整数倍旋转，图像完全填满新画布，不存在背景填充问题。

### 2.3 旋转与分辨率

当旋转 90° 或 270° 时，图像的宽度和高度互换：

- 原 `ImageWidth` × `ImageLength` 变为 `ImageLength` × `ImageWidth`。
- TWAIN 规范中 `XResolution` 和 `YResolution` 分别对应水平和垂直方向；旋转后，物理上的“水平”和“垂直”也互换了。
- 在本项目中，`XResolution` 和 `YResolution` 始终由 `ScannerSettings.x_resolution / y_resolution` 决定，且目前 UI 只暴露同一档 DPI，因此即使宽高互换，水平和垂直分辨率仍保持相同，不会产生矛盾。

### 2.4 TWAIN 中没有标准的 Rotation Capability

与 Flip 类似，TWAIN 规范没有定义标准的 `ICAP_ROTATION` capability 来让应用直接设置 90°/180°/270° 旋转。因此：

- rotation 只能作为 **DS 内部设置** 存在，通过 settings UI 暴露给用户。
- 应用如果不显示 UI，则 rotation 保持默认值 `0`。
- 某些高级 TWAIN 扫描仪支持 `ICAP_ROTATION`（取值常为 0..359），但那是厂商扩展，本项目不实现。

### 2.5 与 DIB 扫描线输出的关系

`VirtualScanner::getScanStrip` 按 DIB 惯例 **自底向上** 输出扫描线。旋转操作在 `preScanPrep()` 阶段完成，发生在 `calculateRowParams()` 之前。旋转后：

- `FreeImage_GetWidth` 和 `FreeImage_GetHeight` 已经反映新的宽高。
- `calculateRowParams()` 根据新的宽度重新计算 `dest_bytes_per_row_` 和总行数。
- 后续 `getImageInfo()` 向 TWAIN 应用上报的 `ImageWidth` / `ImageLength` 与新图像一致。

## 3. 主要设计决策和原因

### 3.1 值编码：`0 = 0°, 1 = 90°, 2 = 180°, 3 = 270°`

**决策**：用整数编码顺时针 90° 的倍数，0 表示无旋转。

**原因**：

- 与项目中其他枚举型设置（`page_size`、`page_fill_mode`、`flip`）的编码风格保持一致。
- 整数编码在 HTML `select` 和 C++ 结构体之间传递简单，无需字符串解析。
- 只支持 90° 整数倍，避免引入任意角度旋转带来的插值质量和 UI 复杂度问题。

### 3.2 使用 `FreeImage_Rotate` 而不是手动像素重排

**决策**：直接调用 `FreeImage_Rotate(dib_, angle, nullptr)`，然后替换 `dib_` 指针。

**原因**：

- FreeImage 对 90° 整数倍有优化路径，可以做到无损旋转。
- 自实现 90° 旋转需要分别处理 24-bit、8-bit、1-bit 三种位深的行排列、4 字节对齐、调色板等细节，代码量大且容易出错。
- 1-bit 图像的位打包方向（MSB 优先）在手动旋转时特别容易处理错。

### 3.3 变换顺序：缩放 → 旋转 → 翻转 → 像素类型转换

`preScanPrep()` 中的调用顺序确定为：

```
ensure24BitDib()
  → applyPageSizeScaling()
  → applyRotation()
  → applyFlip()
  → applyPixelFormat()
```

**原因**：

1. **缩放必须在旋转之前**：Page Size Scaling 按用户选择的物理页面（Letter / Legal / A4 / A5）和 DPI 计算目标像素尺寸。如果在旋转后再缩放，目标宽高对应关系会混乱（例如 90° 旋转后，原本按纵向页面计算的宽高会颠倒）。
2. **旋转放在翻转之前**：旋转改变宽高，翻转不改变宽高。把翻转放在旋转之后，用户先决定“纸张方向”，再决定“是否镜像修正”，语义更清晰。
3. **像素类型转换放在最后**：BW 阈值化和灰度转换涉及采样或量化，应该在几何变换全部完成后再做，避免多次失真。

### 3.4 不暴露为 TWAIN Capability

**决策**：rotation 只通过 settings UI 配置，不注册任何 TWAIN capability。

**原因**：

- TWAIN 规范没有标准的 90°/180°/270° rotation capability，自行定义私有 capability 会降低兼容性。
- 通用 TWAIN 宿主程序不会主动设置该私有 capability，最终还是依赖 UI。
- 保持 capability 集合精简，减少维护负担。

### 3.5 旋转后释放旧 DIB

**决策**：`FreeImage_Rotate` 返回新 DIB 后，立即 `FreeImage_Unload(dib_)` 并把 `dib_` 指向新 DIB。

**原因**：

- 避免内存泄漏。旧 DIB 如果不释放，每次旋转都会留下一份未释放的位图内存。
- 后续所有处理（翻转、像素类型转换、扫描线读取）都基于 `dib_` 进行，必须保证 `dib_` 始终指向当前有效的位图。

## 4. 架构各层次的改动

### 4.1 本地化层（`localization.h` / `localization.cpp`）

新增 5 条字符串常量：

| 字段 | 英文 | 中文 |
|---|---|---|
| `rotation` | `"Rotation:"` | `"旋转："` |
| `rotation_0deg` | `"0 degree"` | `"0 度"` |
| `rotation_90deg` | `"90 degree"` | `"90 度"` |
| `rotation_180deg` | `"180 degree"` | `"180 度"` |
| `rotation_270deg` | `"270 degree"` | `"270 度"` |

在 `Strings` 结构体中按字段顺序追加，保证两个语言初始化列表字段顺序一致。

### 4.2 设置 UI 层（`settings_server.h` / `settings_server.cpp`）

- `SettingsUiResult` 增加 `int rotation; // 0=0 deg, 1=90 deg, 2=180 deg, 3=270 deg clockwise`。
- HTML 生成：在 Page Size `</select>` 结束后插入 Rotation 的 `label` + `select`，包含四个 `option`。
- `doScan()` JavaScript：把 `p.rotation = val('rotation','');` 加入提交参数列表。
- `parseFormData()`：用 `std::atoi(params["rotation"].c_str())` 解析并写入 `result_.rotation`。

### 4.3 扫描器核心层（`virtual_scanner.h` / `virtual_scanner.cpp`）

- `ScannerSettings` 增加 `int rotation;` 字段。
- `resetScanner()` 初始化 `settings_.rotation = 0;`（0°）。
- `VirtualScanner` 类声明并新增私有方法 `bool applyRotation();`。
- `preScanPrep()` 在 `applyPageSizeScaling()` 之后、`applyFlip()` 之前调用 `applyRotation()`。
- `applyRotation()` 实现：
  - `rot == 0`：直接返回 `true`（无操作）。
  - `rot` 为 1/2/3：计算 `angle = -rot * 90.0`，调用 `FreeImage_Rotate(dib_, angle, nullptr)`。
  - 释放旧 `dib_`，接管新返回的 DIB。
  - `rot` 越界：视为 `0`，不旋转。

### 4.4 TWAIN 数据源层（`twain_data_source.cpp`）

- `enableDs()` 打开 settings UI 前，把 `scanner_.getSettings().rotation` 读到 `ui_result.rotation`，让 UI 回显上一次选择。
- 用户点击 Scan 后，把 `ui_result.rotation` 写回 `ui_settings.rotation`，再 `scanner_.setSettings(ui_settings)`。

### 4.5 改动文件清单

```
src/localization.h
src/localization.cpp
src/virtual_scanner.h
src/virtual_scanner.cpp
src/settings_server.h
src/settings_server.cpp
src/twain_data_source.cpp
docs/Rotation_design.md
```

## 5. 局限性和下一步工作

### 5.1 局限性

1. **无法通过 TWAIN Capability 设置**：外部 TWAIN 宿主如果不打开 DS UI，就无法启用 rotation。对于需要静默扫描并按固定角度输出结果的工作流，这不够灵活。
2. **仅支持 90° 整数倍**：不支持任意角度旋转（如 45°）。用户如果扫描斜置文档，无法在本 DS 内做透视校正或自由角度旋转。
3. **没有状态持久化**：`ScannerSettings` 中的 `rotation` 值在每次 `resetScanner()` 时都会重置为 `0°`。虽然单张扫描的设置有效，但会话结束后不会持久化到磁盘。
4. **90°/270° 旋转后页面方向可能不符合预期**：例如用户选择 A4 纵向页面 + 90° 旋转，实际输出会变成 A4 横向方向（宽高互换）。UI 没有动态提示这种变化。
5. **预览图不可见**：settings UI 只有控件没有预览，用户无法在选择 rotation 时实时看到旋转效果。

### 5.2 下一步工作

1. **持久化用户上次选择**：把 `rotation`（以及 flip、page_fill_mode 等）一起写入 `info.json` 或单独的配置文件，下一次打开 UI 时默认恢复上次设置。
2. **增加 TWAIN 私有 capability（可选）**：如果未来有特定宿主需要静默设置 rotation，可以注册一个私有 capability（例如 `CAP_CUSTOM_ROTATION`），但需谨慎评估兼容性。
3. **支持任意角度旋转**：如果需要处理斜置文档，可以暴露 0°–359° 的角度输入，并使用 `FreeImage_Rotate` 的插值模式（如双线性）。需要权衡插值质量与性能。
4. **实时预览**：在 settings UI 中增加缩略图，让用户选择 rotation / flip / page fill 时即时看到效果，降低误操作率。
5. **页面方向联动提示**：当用户选择 90° 或 270° 时，在 UI 中动态提示“页面方向将变为横向/纵向”，帮助用户理解宽高互换的结果。
6. **单元测试补充**：为 `applyRotation()` 增加测试，覆盖 0°/90°/180°/270° 在 1-bit / 8-bit / 24-bit 三种像素类型下的输出尺寸、行顺序、调色板正确性。

</details>

