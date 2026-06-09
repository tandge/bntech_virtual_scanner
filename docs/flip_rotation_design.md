# Flip & Rotation Design

Design notes for the **Rotation** and **Flip** options in the BN Tech Virtual Scanner settings UI and image processing pipeline.

<details open>
<summary>中文说明</summary>

## 1. 需求

在 settings UI 的扫描设置区增加并维护两个图像方向相关配置项：

- **Rotation**：按顺时针方向旋转图像。
- **Flip**：按水平或垂直方向镜像翻转图像。

这两个选项用于在扫描输出前修正图像方向，使用户不需要在宿主软件中二次处理。

### 1.1 Rotation 需求

settings UI 提供 **Rotation** 下拉框，包含：

- `0 degree`：不旋转，默认值。
- `90 degree`：顺时针旋转 90°。
- `180 degree`：顺时针旋转 180°。
- `270 degree`：顺时针旋转 270°。

功能要求：

- 用户选择后，值必须从 settings UI 回写到 `ScannerSettings`。
- Rotation 必须作用于下一张扫描图像。
- Rotation 必须同时影响：
  - **Native Transfer** 返回给 TWAIN 宿主的 DIB。
  - **File Transfer** 保存到磁盘的图片文件。
- 90° / 270° 旋转后，输出宽高必须互换。
- `TW_IMAGEINFO` 中上报的 `ImageWidth` / `ImageLength` 必须与旋转后的真实图像一致。

### 1.2 Flip 需求

settings UI 提供 **Flip** 下拉框，包含：

- `None`：不翻转，默认值。
- `Horizontal`：水平翻转，左右镜像。
- `Vertical`：垂直翻转，上下镜像。

功能要求：

- 用户选择后，值必须从 settings UI 回写到 `ScannerSettings`。
- Flip 必须作用于下一张扫描图像。
- Flip 必须同时影响 Native Transfer 和 File Transfer。
- Flip 必须能与 Rotation、Page Size、Page Fill、Pixel Type 等现有配置组合使用。

### 1.3 非功能性需求

- 改动尽量小，不引入新的外部依赖。
- 继续复用 FreeImage 完成图像几何变换。
- 支持中英文 UI 文案。
- 默认值保持为无旋转、无翻转，保证老用户升级后行为不变。
- 几何变换在像素类型转换前完成，避免对 1-bit / 8-bit 图像做复杂位级几何处理。

## 2. 相关领域知识

### 2.1 Rotation 与 Flip 的区别

| 配置 | 几何意义 | 是否改变宽高 | 示例 |
|---|---|---:|---|
| Rotation 90° / 270° | 绕图像中心旋转 | 是 | 纵向页面变横向页面 |
| Rotation 180° | 中心对称旋转 | 否 | 图像上下左右都倒转 |
| Horizontal Flip | 沿垂直中轴镜像 | 否 | 左右交换 |
| Vertical Flip | 沿水平中轴镜像 | 否 | 上下交换 |

Rotation 用于改变纸张方向；Flip 用于修正镜像方向。两者都属于几何变换，但用户意图不同。

### 2.2 FreeImage Rotation API

FreeImage 提供旋转函数：

```c
FIBITMAP* FreeImage_Rotate(FIBITMAP* dib, double angle, const void* bkcolor);
```

特点：

- 返回新的 `FIBITMAP*`，不是原地修改。
- 调用方必须释放旧 DIB 并接管新 DIB。
- FreeImage 中正角度表示逆时针，负角度表示顺时针。
- 因此顺时针 90° 使用 `-90.0`。
- 对 90° 整数倍旋转，FreeImage 可使用像素级路径，避免任意角度插值造成模糊。

### 2.3 FreeImage Flip API

FreeImage 提供两个翻转函数：

```c
BOOL FreeImage_FlipHorizontal(FIBITMAP* dib);
BOOL FreeImage_FlipVertical(FIBITMAP* dib);
```

特点：

- 原地修改传入的 DIB。
- 不需要替换 `dib_` 指针。
- 不改变图像宽高。
- 通道顺序不变，只改变像素位置。

### 2.4 与 DIB 扫描线输出的关系

项目通过 `VirtualScanner::getScanStrip()` 按 DIB 惯例输出扫描线。DIB 通常是自底向上存储，项目内部已经统一处理扫描线顺序。

Rotation 和 Flip 都在 `preScanPrep()` 阶段执行，发生在：

- `calculateRowParams()` 之前。
- `getImageInfo()` 上报图像信息之前。
- Native Transfer / File Transfer 读取图像数据之前。

因此几何变换完成后，后续流程只看到已经处理好的最终图像，不需要知道用户曾经选择过什么 rotation / flip。

### 2.5 TWAIN Capability 的限制

TWAIN 标准没有本项目直接使用的标准 `ICAP_ROTATION` 或 `ICAP_FLIP` 能力项。

因此：

- Rotation / Flip 作为 DS 内部设置存在。
- 外部 TWAIN 宿主不能通过标准 `DAT_CAPABILITY` 直接设置它们。
- 用户需要通过 settings UI 设置。
- 如果宿主不显示 DS UI，则保持默认值。

## 3. 设计决策和原因

### 3.1 使用简单整数编码

**决策**：Rotation 和 Flip 都使用整数编码。

Rotation：

| 值 | 含义 |
|---:|---|
| 0 | 0° |
| 1 | 顺时针 90° |
| 2 | 顺时针 180° |
| 3 | 顺时针 270° |

Flip：

| 值 | 含义 |
|---:|---|
| 0 | None |
| 1 | Horizontal |
| 2 | Vertical |

**原因**：

- 与项目中 `page_size`、`page_fill_mode`、`file_format` 等设置风格一致。
- HTML `select` 提交和 C++ 解析简单。
- 默认值 `0` 表示无操作，便于初始化和向后兼容。

### 3.2 只支持 90° 整数倍旋转

**决策**：Rotation 只提供 0° / 90° / 180° / 270°。

**原因**：

- 扫描文档最常见的方向修正就是 90° 倍数。
- 90° 倍数旋转不会引入任意角度插值造成的模糊。
- UI 简单，用户选择明确。
- 避免新增角度输入校验、背景填充、裁切策略等复杂逻辑。

### 3.3 使用 FreeImage 而不是手写像素变换

**决策**：Rotation 使用 `FreeImage_Rotate`，Flip 使用 `FreeImage_FlipHorizontal` / `FreeImage_FlipVertical`。

**原因**：

- 项目已经依赖 FreeImage。
- FreeImage 能处理不同位深、调色板和行对齐。
- 手写几何变换需要处理 1-bit 位打包、8-bit 调色板、24-bit BGR、4 字节行对齐等细节，风险高且代码量大。
- 复用 FreeImage 符合“改动尽量少”的要求。

### 3.4 变换顺序：缩放 → 旋转 → 翻转 → 像素类型转换

`preScanPrep()` 中的顺序为：

```text
ensure24BitDib()
  → applyPageSizeScaling()
  → applyRotation()
  → applyFlip()
  → applyPixelFormat()
```

**原因**：

1. **先保证 24-bit**：几何处理前统一像素格式，降低后续处理复杂度。
2. **先 Page Size Scaling**：页面大小和 DPI 决定目标像素尺寸，应先生成目标画布。
3. **Rotation 在 Flip 前**：Rotation 表示纸张方向，Flip 表示镜像修正；先决定方向，再做镜像更符合用户心智。
4. **Pixel Type 最后转换**：BW 阈值化和灰度转换会降低颜色信息，应在所有几何变换后再做，避免重复损失。

### 3.5 Rotation 返回新 DIB，Flip 原地修改

**决策**：

- Rotation：接收 `FreeImage_Rotate` 返回的新 DIB，释放旧 DIB。
- Flip：直接使用 FreeImage 原地翻转结果。

**原因**：

- 与 FreeImage API 语义一致。
- 避免 Rotation 产生内存泄漏。
- 避免 Flip 做不必要的 DIB 复制。

### 3.6 不暴露为 TWAIN Capability

**决策**：不新增标准或私有 TWAIN capability。

**原因**：

- TWAIN 标准没有通用 Flip capability。
- 自定义 capability 对通用 TWAIN 宿主基本不可见。
- 私有 capability 会增加兼容性和维护成本。
- 当前需求主要来自 settings UI，内部设置即可满足。

## 4. 架构各层次的改动

### 4.1 本地化层：`localization.h` / `localization.cpp`

新增或维护 Rotation 文案：

| 字段 | 英文 | 中文 |
|---|---|---|
| `rotation` | `Rotation:` | `旋转：` |
| `rotation_0deg` | `0 degree` | `0 度` |
| `rotation_90deg` | `90 degree` | `90 度` |
| `rotation_180deg` | `180 degree` | `180 度` |
| `rotation_270deg` | `270 degree` | `270 度` |

新增或维护 Flip 文案：

| 字段 | 英文 | 中文 |
|---|---|---|
| `flip` | `Flip:` | `翻转：` |
| `flip_none` | `None` | `无` |
| `flip_horizontal` | `Horizontal` | `水平翻转` |
| `flip_vertical` | `Vertical` | `垂直翻转` |

### 4.2 Settings UI 层：`settings_server.h` / `settings_server.cpp`

`SettingsUiResult` 增加字段：

```cpp
int rotation; // 0=0 deg, 1=90 deg, 2=180 deg, 3=270 deg clockwise.
int flip;     // 0=None, 1=Horizontal, 2=Vertical.
```

HTML 构建：

- 在扫描设置区输出 Rotation 下拉框。
- 在 Rotation 下方输出 Flip 下拉框。
- 使用 `sel(value, current)` 保持 UI 回显当前值。

提交逻辑：

```js
p.rotation = val('rotation','');
p.flip = val('flip','');
```

解析逻辑：

```cpp
result_.rotation = std::atoi(params["rotation"].c_str());
result_.flip = std::atoi(params["flip"].c_str());
```

### 4.3 扫描器核心层：`virtual_scanner.h` / `virtual_scanner.cpp`

`ScannerSettings` 增加字段：

```cpp
int rotation;
int flip;
```

初始化：

```cpp
settings_.rotation = 0;
settings_.flip = 0;
```

新增处理方法：

```cpp
bool applyRotation();
bool applyFlip();
```

Rotation 实现要点：

- `0`：直接返回 true。
- `1/2/3`：计算 `angle = -rot * 90.0`。
- 调用 `FreeImage_Rotate(dib_, angle, nullptr)`。
- 成功后释放旧 `dib_` 并接管新 DIB。
- 越界值按无旋转处理。

Flip 实现要点：

- `0`：直接返回 true。
- `1`：调用 `FreeImage_FlipHorizontal(dib_)`。
- `2`：调用 `FreeImage_FlipVertical(dib_)`。
- 越界值按无翻转处理。

处理顺序：

```cpp
ensure24BitDib();
applyPageSizeScaling();
applyRotation();
applyFlip();
applyPixelFormat();
calculateRowParams();
```

### 4.4 TWAIN 数据源层：`twain_data_source.cpp`

打开 settings UI 前：

```cpp
ui_result.rotation = scanner_.getSettings().rotation;
ui_result.flip = scanner_.getSettings().flip;
```

用户点击 Scan 后：

```cpp
ui_settings.rotation = ui_result.rotation;
ui_settings.flip = ui_result.flip;
scanner_.setSettings(ui_settings);
```

效果：

- UI 可以回显当前值。
- 用户确认后，新值进入扫描器核心设置。
- Native Transfer 和 File Transfer 共享同一处理后的 DIB。

### 4.5 TWAIN 输出信息层

Rotation / Flip 不直接修改 TWAIN capability，但会影响最终图像。

尤其是 Rotation：

- 90° / 270° 会改变 `FreeImage_GetWidth(dib_)` 和 `FreeImage_GetHeight(dib_)`。
- 后续 `getImageInfo()` 使用变换后的宽高上报 `TW_IMAGEINFO`。
- `calculateRowParams()` 使用变换后的宽度计算行字节数。

Flip 不改变宽高，因此不影响 `ImageWidth` / `ImageLength`，只影响像素排列。

### 4.6 改动文件清单

```text
src/localization.h
src/localization.cpp
src/settings_server.h
src/settings_server.cpp
src/virtual_scanner.h
src/virtual_scanner.cpp
src/twain_data_source.cpp
docs/flip_rotation_design.md
```

## 5. 局限性和风险

### 5.1 无法通过标准 TWAIN Capability 设置

Rotation / Flip 只能通过 settings UI 设置。外部宿主如果不显示 UI，就无法通过标准 TWAIN API 启用这些功能。

风险：

- 静默扫描工作流无法指定 rotation / flip。
- 自动化集成场景灵活性不足。

### 5.2 不支持任意角度旋转

当前只支持 90° 整数倍，不支持 1°、45°、任意角度或自动纠偏。

原因是任意角度会引入：

- 插值模糊。
- 背景填充。
- 裁切策略。
- UI 输入和校验复杂度。

### 5.3 不支持同时水平和垂直 Flip

Flip 当前是单选：None / Horizontal / Vertical。

局限：

- 无法直接选择 Horizontal + Vertical。
- 用户可用 180° Rotation 间接获得类似效果，但语义不同。

### 5.4 Rotation 和 Flip 的顺序固定

当前固定为：

```text
Rotation → Flip
```

对于大多数用户，这符合“先调纸张方向，再做镜像修正”的理解。但在数学上，`Rotate → Flip` 和 `Flip → Rotate` 可能得到不同方向的结果。

风险：

- 少数用户可能期望另一种组合顺序。
- UI 没有解释组合顺序。

### 5.5 设置没有持久化

Rotation / Flip 默认在重置扫描器时回到 0。当前没有写入磁盘配置文件。

风险：

- 用户每次打开 DS 可能需要重新选择。
- 批量工作流中不够方便。

### 5.6 没有实时预览

settings UI 当前只有下拉框，没有图像预览。

风险：

- 用户无法在点击 Scan 前确认最终方向。
- Rotation + Flip 组合时可能需要多次试错。

### 5.7 图像 metadata 和方向标签

当前实现直接改变像素数据，而不是写 EXIF orientation 或类似 metadata。

优点：

- TWAIN 宿主和普通图片查看器看到的是已经变换后的像素。

局限：

- 不保留“原始方向 + orientation tag”的语义。
- 对某些希望保留原图像素的场景不适用。

## 6. 下一步工作

1. **持久化设置**
   - 将 `rotation`、`flip` 与 page size、resolution 等一起保存到配置文件。
   - 下次打开 settings UI 时恢复上次选择。

2. **增加组合 Flip 选项**
   - 可考虑增加 `Both`，表示同时水平和垂直翻转。
   - 或改为两个复选框：Horizontal / Vertical。

3. **增加实时预览**
   - 在 settings UI 中显示缩略图。
   - 当用户调整 Rotation / Flip / Page Fill 时实时更新预览。

4. **补充自动化测试**
   - Rotation：验证 0° / 90° / 180° / 270° 输出尺寸和像素位置。
   - Flip：验证水平 / 垂直翻转后的像素位置。
   - 组合：验证 Rotation + Flip + Pixel Type 的顺序和结果。

5. **可选：私有 TWAIN capability**
   - 如果未来有特定宿主需要静默扫描时设置 Rotation / Flip，可考虑增加私有 capability。
   - 需要谨慎评估兼容性，不建议影响标准 capability 行为。

6. **UI 提示优化**
   - 当选择 90° / 270° 时提示“输出宽高将互换”。
   - 对 Rotation + Flip 的组合顺序增加简短说明。

</details>
