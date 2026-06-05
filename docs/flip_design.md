# Flip Design (None / Horizontal / Vertical)

Design notes for adding a flip option to the BN Tech Virtual Scanner settings UI and image processing pipeline.

<details open>
<summary>中文说明</summary>

## 1. 需求

在 settings UI 的扫描设置区增加 **Flip**（翻转）选项，允许用户在扫描输出前对图像进行水平或垂直翻转，默认不翻转。

主要功能需求：

- settings UI 提供一个下拉列表框，位于 **Rotation** 配置项正下方，包含三个选项：
  - `None` — 不翻转（默认）
  - `Horizontal` — 水平翻转（左右镜像）
  - `Vertical` — 垂直翻转（上下镜像）
- 用户选择的 flip 值必须在 settings UI 关闭后正确回写到 `ScannerSettings`，并作用于下一张扫描图像。
- 翻转必须同时作用于 **Native Transfer** 返回的 DIB 和 **File Transfer** 写出的文件。
- 翻转必须与现有的 Rotation、Page Size Scaling、Pixel Type 转换等功能正交组合：任意顺序选择都应产生预期结果。
- 不影响任何已有控件（Rotation、Page Fill、Color Mode、Resolution 等）的原有行为。

非功能性需求：

- 改动范围尽量小，不引入新的外部依赖。
- 继续复用 FreeImage 完成图像几何变换。
- 支持现有中英双语本地化（英文：`None` / `Horizontal` / `Vertical`；中文：`无` / `水平翻转` / `垂直翻转`）。
- 默认值为 `None`，老用户升级后行为不变。

## 2. 领域知识

### 2.1 翻转与旋转的区别

| 变换 | 几何意义 | 常见实现 |
|---|---|---|
| 旋转 (Rotation) | 绕图像中心转动 90°/180°/270° | `FreeImage_Rotate` |
| 水平翻转 (Horizontal Flip) | 沿垂直中轴镜像，左右互换 | `FreeImage_FlipHorizontal` |
| 垂直翻转 (Vertical Flip) | 沿水平中轴镜像，上下颠倒 | `FreeImage_FlipVertical` |

翻转是 **镜像变换**，不是简单的平移或旋转。水平翻转 + 垂直翻转 等价于 180° 旋转，但视觉意图不同：用户选择 Flip 是为了修正原图方向，而不是旋转纸张。

### 2.2 FreeImage 翻转 API

FreeImage 提供两个原地翻转函数：

```c
DLL_API BOOL DLL_CALLCONV FreeImage_FlipHorizontal(FIBITMAP *dib);
DLL_API BOOL DLL_CALLCONV FreeImage_FlipVertical(FIBITMAP *dib);
```

特点：

- **原地操作**：直接修改传入的 `FIBITMAP*`，不需要释放旧句柄再指向新句柄（与 `FreeImage_Rotate` 不同）。
- **返回 BOOL**：`TRUE` 表示成功，`FALSE` 表示失败（如传入空指针）。
- **通道顺序不变**：对于 24-bit BGR，翻转只是改变像素存储顺序，不会改动 R/G/B 分量本身。
- **支持所有位深**：1-bit / 8-bit / 24-bit 图像都可以直接调用。

### 2.3 TWAIN 中没有标准的 Flip Capability

TWAIN 规范没有定义类似 `ICAP_FLIP` 的标准 capability。因此：

- 无法通过 TWAIN API 让外部应用直接设置 flip。
- flip 只能作为 **DS 内部设置** 存在，通过 settings UI 暴露给用户。
- 应用如果不显示 UI，则 flip 保持默认值 `None`。

### 2.4 与 DIB 扫描线输出的关系

`VirtualScanner::getScanStrip` 按 DIB 惯例 **自底向上** 输出扫描线。翻转操作在 `preScanPrep()` 阶段完成，发生在 `calculateRowParams()` 之前，因此翻转后的图像已经稳定，后续按行读取无需关心 flip 历史。

## 3. 主要设计决策和原因

### 3.1 值编码：`0 = None, 1 = Horizontal, 2 = Vertical`

**决策**：用整数编码，0 表示无翻转，1 表示水平，2 表示垂直。

**原因**：

- 与项目中其他下拉选项（如 `page_size`、`page_fill_mode`、`rotation`）的编码风格保持一致。
- `0` 作为默认/无操作值，符合直觉，也便于 `resetScanner()` 初始化为 `0` 即可保持向后兼容。
- 避免使用位掩码：水平与垂直翻转是互斥的单选，而非可以同时勾选的多选。

### 3.2 变换顺序：先缩放与旋转，再翻转，最后像素类型转换

`preScanPrep()` 中的调用顺序确定为：

```
ensure24BitDib()
  → applyPageSizeScaling()
  → applyRotation()
  → applyFlip()
  → applyPixelFormat()
```

**原因**：

1. **缩放必须在翻转之前**：先按页大小和 DPI 调整分辨率，再对结果做镜像，避免缩放算法把镜像后的像素插值回非镜像状态。
2. **旋转与翻转顺序可交换**：理论上 `Rotate + Flip` 与 `Flip + Rotate` 的结果在几何上等价于某种组合，但对用户意图来说，旋转是“纸张方向”，翻转是“镜像修正”，把翻转放在旋转之后更自然。
3. **像素类型转换放在最后**：BW 阈值化和灰度转换涉及采样或量化，应该在几何变换全部完成后再做，避免多次失真。

### 3.3 不暴露为 TWAIN Capability

**决策**：flip 只通过 settings UI 配置，不注册任何 TWAIN capability，也不支持应用通过 `DAT_CAPABILITY` 设置。

**原因**：

- TWAIN 规范没有 `ICAP_FLIP` 或等价标准 capability，自行定义私有 capability 会降低与通用 TWAIN 宿主程序的兼容性。
- 大多数 TWAIN 宿主（Twack、NAPS2、Photoshop 导入等）不会主动设置私有 capability，最终还是依赖 UI。
- 保持 capability 集合精简，减少维护负担。

### 3.4 复用 FreeImage 而不是自实现翻转

**决策**：直接调用 `FreeImage_FlipHorizontal` / `FreeImage_FlipVertical`。

**原因**：

- FreeImage 已经正确处理了所有位深、调色板图像和内存对齐。
- 自实现翻转需要处理 1-bit 行打包、4 字节对齐、调色板顺序等细节，容易出错。
- 不会引入新的依赖。

### 3.5 Flip 控件放在 Rotation 下方

**决策**：在 settings UI 的扫描设置区，把 Flip 下拉框紧跟在 Rotation 下拉框之后、Page Fill 之前。

**原因**：

- 翻转与旋转同属“几何方向”类设置，视觉上放在一起最符合用户心智模型。
- Page Fill（拉伸/适应/填充）属于“布局策略”，与 Flip 不是同一语义组，放在 Flip 之后更合理。

## 4. 架构各层次的改动

### 4.1 本地化层（`localization.h` / `localization.cpp`）

新增 4 条字符串常量：

| 字段 | 英文 | 中文 |
|---|---|---|
| `flip` | `"Flip:"` | `"翻转："` |
| `flip_none` | `"None"` | `"无"` |
| `flip_horizontal` | `"Horizontal"` | `"水平翻转"` |
| `flip_vertical` | `"Vertical"` | `"垂直翻转"` |

在 `Strings` 结构体中追加在 `rotation_270deg` 之后、`product_family` 之前，保证两个语言初始化列表字段顺序一致。

### 4.2 设置 UI 层（`settings_server.h` / `settings_server.cpp`）

- `SettingsUiResult` 增加 `int flip; // 0=None, 1=Horizontal, 2=Vertical`。
- HTML 生成：在 Rotation `</select>` 结束后插入 Flip 的 `label` + `select`，包含三个 `option`。
- `doScan()` JavaScript：把 `p.flip = val('flip','');` 加入提交参数列表。
- `parseFormData()`：用 `std::atoi(params["flip"].c_str())` 解析并写入 `result_.flip`。

### 4.3 扫描器核心层（`virtual_scanner.h` / `virtual_scanner.cpp`）

- `ScannerSettings` 增加 `int flip;` 字段。
- `resetScanner()` 初始化 `settings_.flip = 0;`（None）。
- `VirtualScanner` 类声明并新增私有方法 `bool applyFlip();`。
- `preScanPrep()` 在 `applyRotation()` 之后、`applyPixelFormat()` 之前调用 `applyFlip()`。
- `applyFlip()` 实现：
  - `flip == 0`：直接返回 `true`（无操作）。
  - `flip == 1`：调用 `FreeImage_FlipHorizontal(dib_)`。
  - `flip == 2`：调用 `FreeImage_FlipVertical(dib_)`。
  - 其他值：视为无效，按 `None` 处理。

### 4.4 TWAIN 数据源层（`twain_data_source.cpp`）

- `enableDs()` 打开 settings UI 前，把 `scanner_.getSettings().flip` 读到 `ui_result.flip`，让 UI 回显上一次选择。
- 用户点击 Scan 后，把 `ui_result.flip` 写回 `ui_settings.flip`，再 `scanner_.setSettings(ui_settings)`。

### 4.5 改动文件清单

```
src/localization.h
src/localization.cpp
src/virtual_scanner.h
src/virtual_scanner.cpp
src/settings_server.h
src/settings_server.cpp
src/twain_data_source.cpp
docs/flip_design.md
```

## 5. 局限性和下一步工作

### 5.1 局限性

1. **无法通过 TWAIN Capability 设置**：外部 TWAIN 宿主如果不打开 DS UI，就无法启用 flip。对于需要静默扫描并自动镜像的工作流，这不够灵活。
2. **翻转与旋转的组合语义单一**：当前顺序固定为 `Rotate → Flip`。某些用户可能希望 `Flip → Rotate`，项目目前不提供顺序选择。
3. **没有状态持久化**：`ScannerSettings` 中的 `flip` 值在每次 `resetScanner()` 时都会重置为 `None`。虽然 `openDs` 会调用 `resetScanner()`，但 `enableDs` 之前会从当前 settings 读到 UI，所以单张扫描的 flip 设置是有效的；会话结束后不会持久化到磁盘。
4. **不支持同时水平和垂直翻转**：UI 中只能单选，不能同时勾选两个方向。需要同时翻转时，用户只能用 180° 旋转间接实现。
5. **预览图不可见**：settings UI 只有控件没有预览，用户无法在下拉选择时实时看到翻转效果。

### 5.2 下一步工作

1. **持久化用户上次选择**：把 `flip`（以及 rotation、page_fill_mode 等）一起写入 `info.json` 或单独的配置文件，下一次打开 UI 时默认恢复上次设置。
2. **增加 TWAIN 私有 capability（可选）**：如果未来有特定宿主需要静默设置 flip，可以注册一个私有 capability（例如 `CAP_CUSTOM_FLIP`），但需谨慎评估兼容性。
3. **支持组合翻转**：把 flip 从单选改成多选（或增加 `Both` 选项），允许同时水平 + 垂直翻转，等价于 180° 旋转但语义更直接。
4. **实时预览**：在 settings UI 中增加缩略图，让用户选择 flip / rotation / page fill 时即时看到效果，降低误操作率。
5. **单元测试补充**：为 `applyFlip()` 增加测试，覆盖 1-bit / 8-bit / 24-bit 三种像素类型在水平/垂直翻转后的行顺序、调色板不变性、DIB 输出一致性。

</details>

