# Settings UI Preview Design

Design notes for adding a Preview feature to the Settings UI. This document compares several implementation options, explains what it means to extract the `VirtualScanner` image-processing pipeline, and records the recommended rollout plan.

<details open>
<summary>中文说明</summary>

## 1. 背景

当前 Settings UI 由 `SettingsServer::buildHtmlPage()` 动态生成 HTML，通过本地 HTTP server 和默认浏览器显示。用户可以配置 Color Mode、Resolution、Page Size、Page Fill Mode、Rotation、Flip、Transfer Mode、File Format 和输出路径。

真实扫描时，图像处理主要在 `VirtualScanner` 中完成：

```text
VirtualScanner::acquireImage()
  ├── 从 %APPDATA%\bntech\images 选择下一张图片
  ├── FreeImage_Load 加载图片
  └── VirtualScanner::preScanPrep()
      ├── ensure24BitDib()
      ├── applyPageSizeScaling()
      ├── applyRotation()
      ├── applyFlip()
      ├── applyPixelFormat()
      └── calculateRowParams()
```

Preview 的目标是在用户点击 Scan 前，让用户看到当前设置的大致效果或真实输出效果。

## 2. 可选方案

### 2.1 方案一：纯前端示意 Preview

在 Settings UI 中添加一个预览区域，用内置 SVG、Canvas 或 CSS 画一张示意图，然后根据当前设置做视觉变化。

| 设置 | 前端实现 |
|---|---|
| Rotation | CSS `transform: rotate(...)` |
| Flip | CSS `scaleX(-1)` / `scaleY(-1)` |
| Stretch | `object-fit: fill` 或 Canvas 拉伸 |
| Fit with padding | `object-fit: contain` |
| Fill and crop | `object-fit: cover` |
| Grayscale | CSS `filter: grayscale(1)` |
| Black & White | Canvas threshold，或近似显示 |

优点：

- 实现最快。
- 不需要改 C++ 图像处理逻辑。
- 不需要新增 HTTP endpoint。
- 性能最好，修改下拉框后可以立即刷新。
- 不涉及真实图片路径、图片格式和二进制 HTTP response。

缺点：

- 只是效果示意，不是真实扫描图片。
- BW 阈值、FreeImage 缩放算法、裁剪边缘和 DPI 影响不能完全一致。
- 用户可能误以为它就是最终扫描结果。

适合：快速 MVP，用于解释 Rotation、Flip、Page Fill Mode 的含义。

### 2.2 方案二：前端加载真实源图，浏览器侧做近似变换

新增 endpoint：

```text
GET /source-preview
```

该 endpoint 返回当前用于预览的源图片。Settings UI 使用 `<img>` 或 `<canvas>` 显示该图片，再用 CSS / JS 根据设置做变换。

示例：

```html
<img id="previewImg" src="/source-preview">
```

```js
img.style.objectFit = fillMode == 0 ? 'fill'
                  : fillMode == 1 ? 'contain'
                  : 'cover';
img.style.transform = 'rotate(90deg) scaleX(-1)';
```

优点：

- 显示真实源图片，比纯示意图更直观。
- C++ 端只需要把图片文件通过 HTTP response 返回。
- Rotation / Flip / Page Fill 可即时刷新。
- 前端交互相对简单。

缺点：

- 预览仍然不等于真实扫描输出。
- 浏览器对 TIFF 等格式支持有限。
- Pixel Type 的 BW / Grayscale 处理与 FreeImage 不一定一致。
- Page Size / DPI 对最终像素尺寸的影响只能近似映射到预览框。
- 必须保证 preview 不推进真实扫描的 `current_image_index_`。

适合：希望显示真实图片，但可以接受浏览器侧近似效果。

### 2.3 方案三：后端 `/preview` 实时生成真实预览图

新增 endpoint：

```text
GET /preview?pixeltype=2&resolution=300&pagesize=2&pagefillmode=1&rotation=1&flip=0
```

C++ 收到请求后：

1. 解析 URL 参数，构造扫描设置。
2. 加载当前预览源图片。
3. 使用与真实扫描一致的 FreeImage 处理逻辑。
4. 将处理后的图片缩小到 UI 预览区域大小。
5. 输出 PNG 或 JPEG 给浏览器显示。

Settings UI 中使用：

```html
<img id="preview" src="/preview?...">
```

用户修改设置时更新 `src`：

```js
preview.src = '/preview?' + qs + '&t=' + Date.now();
```

优点：

- 预览最接近真实扫描结果。
- 可以统一通过 FreeImage 解码 PNG/JPG/BMP/TIFF/WEBP/GIF 等源格式。
- Rotation、Flip、Page Fill、Pixel Type 都可以与最终输出一致。
- 前端逻辑简单，只需要刷新 `<img>`。

缺点：

- C++ 改动较大。
- 如果直接复制 `VirtualScanner` 里的处理逻辑，会造成维护问题。
- 每次设置变化都生成图片，性能压力较大，需要 debounce，例如 200-300ms。
- 需要处理 binary HTTP response、Content-Type、缓存控制和错误图片。
- 必须避免 preview 修改真实扫描状态。

适合：正式的所见即所得 Preview。

### 2.4 方案四：后端生成临时 preview 文件，前端加载静态结果

和方案三类似，但不是直接在 HTTP response 中返回图片二进制，而是：

1. `/preview-update?...` 触发 C++ 生成临时图片。
2. 图片保存到 `%TEMP%` 或应用自己的临时目录。
3. Settings UI 通过另一个 URL 加载该文件。

示例：

```html
<img src="/preview-file?t=timestamp">
```

优点：

- 比直接 streaming binary response 更容易调试。
- 可以直接打开临时 preview PNG 检查结果。
- 可以做简单缓存，参数不变时不重复生成。
- 对现有 socket HTTP server 的改造可以相对温和。

缺点：

- 需要管理临时文件生命周期。
- 多个应用或多个 DS 实例同时打开时，文件名要避免冲突。
- 需要 cache busting，否则浏览器可能显示旧图。
- 本质仍然是后端真实生成，复杂度接近方案三。

适合：当前 HTTP server 返回二进制图片不方便时，作为过渡方案。

### 2.5 方案五：增加 Preview 按钮，点击后才生成

不做实时自动刷新，而是在 Settings UI 中增加按钮：

```text
[Preview] [Cancel] [Scan]
```

用户点击 Preview 时才请求 `/preview?...`。

优点：

- 性能压力小。
- 实现简单，不需要频繁 debounce。
- 不会因为用户快速切换设置产生大量 preview 请求。
- 适合后端真实生成 preview 的第一版。

缺点：

- 交互不如实时预览顺滑。
- 用户每次修改参数后都需要手动点击 Preview。

适合：第一版真实图片 Preview，希望降低实现和性能风险。

### 2.6 方案六：抽出 `VirtualScanner` pipeline，Scan 和 Preview 共用处理逻辑

这是架构层面的方案。它不是一种单独的 UI 交互，而是方案三、四、五要做到高一致性时最推荐的底层实现方式。

核心做法是把 `VirtualScanner` 内部的图像处理流程拆成一个独立、无状态的 image pipeline，例如：

```cpp
FIBITMAP* BuildProcessedImage(
    const std::string& image_path,
    const ScannerSettings& settings);
```

真实扫描调用它：

```cpp
dib_ = image_pipeline::BuildProcessedImage(image_path, settings_);
```

Settings UI Preview 也调用它：

```cpp
FIBITMAP* preview_dib = image_pipeline::BuildProcessedImage(
    preview_image_path, preview_settings);
```

优点：

- Preview 和真实扫描一致性最高。
- 后续新增图像设置时，只需要改一套 pipeline。
- Preview 不需要调用 `VirtualScanner::acquireImage()`，因此不会污染真实扫描状态。
- 可以避免 Settings UI 中复制一份类似但不完全相同的图像处理代码。

缺点：

- 需要一定重构。
- 要梳理 `VirtualScanner` 当前的状态变量，例如 `dib_`、`settings_`、`current_image_index_`、`scan_line_`。
- 需要确保新 pipeline 对 FreeImage bitmap 的 ownership 规则清晰。

适合：正式长期方案。

### 2.7 方案七：改成 Native UI 或 WebView2 内嵌 Preview

不再依赖默认浏览器打开 Settings HTML，而是改成 Win32 Dialog + GDI/GDI+、WebView2 或自定义窗口内嵌浏览器控件。

优点：

- 窗口大小、位置、关闭行为完全可控。
- 不依赖默认浏览器策略。
- Preview 可以做得更像真实扫描仪 UI。
- 可以支持更复杂的图片显示、缩放、拖拽等交互。

缺点：

- 改动最大。
- WebView2 会引入运行时依赖。
- Native UI 开发和维护成本较高。
- 当前项目已经基于 HTTP + browser，切换 UI 架构风险较大。

适合：Settings UI 未来要升级成完整配置中心。暂不建议作为当前 Preview 功能的第一步。

## 3. 方案对比

| 方案 | 是否显示真实图片 | 与真实扫描一致性 | 实现复杂度 | 推荐程度 |
|---|---:|---:|---:|---:|
| 纯前端示意 Preview | 否 | 低 | 低 | 适合 MVP |
| 前端加载源图 + CSS/JS 变换 | 是 | 中低 | 中 | 可用 |
| 后端 `/preview` 实时生成 | 是 | 高 | 中高 | 推荐正式方案 |
| 后端生成临时文件 | 是 | 高 | 中 | 适合过渡 |
| Preview 按钮手动生成 | 是 | 高 | 中 | 适合第一版真实预览 |
| 抽出 `VirtualScanner` pipeline | 是 | 最高 | 高 | 最佳长期方案 |
| Native UI / WebView2 | 是 | 高 | 很高 | 暂不建议 |

## 4. 什么是“抽出 `VirtualScanner` pipeline”

### 4.1 当前结构的问题

目前 `VirtualScanner` 中的处理函数大多直接操作成员变量：

```cpp
FIBITMAP* dib_;
ScannerSettings settings_;
```

例如：

```cpp
bool VirtualScanner::applyRotation() {
  if (dib_ == nullptr) return false;
  int rot = settings_.rotation;
  if (rot < 0 || rot > 3) rot = 0;
  if (rot == 0) return true;

  double angle = -static_cast<double>(rot) * 90.0;
  FIBITMAP* rotated = FreeImage_Rotate(dib_, angle, nullptr);
  if (rotated == nullptr) return false;

  FreeImage_Unload(dib_);
  dib_ = rotated;
  return true;
}
```

这类函数依赖 `this->dib_` 和 `this->settings_`，只能由 `VirtualScanner` 对象调用。

如果 Settings UI Preview 直接调用 `VirtualScanner::acquireImage()` 或复用同一个 `VirtualScanner` 对象，可能产生副作用：

- 推进 `current_image_index_`。
- 修改 `scan_line_`。
- 替换或释放当前 `dib_`。
- 改变真实扫描正在使用的状态。
- 影响后续 TWAIN transfer。

Preview 应该是只读、无副作用的：输入图片和设置，输出预览图，不改变真实扫描状态。

### 4.2 pipeline 的含义

这里的 pipeline 指固定的图像处理流水线：

```text
输入：image_path + ScannerSettings
        │
        ▼
加载图片 FreeImage_Load
        │
        ▼
转换成 24-bit
        │
        ▼
按 Page Size / Fill Mode 缩放、补白或裁剪
        │
        ▼
Rotation
        │
        ▼
Flip
        │
        ▼
Pixel Type：RGB / Grayscale / BW
        │
        ▼
写入 DPI metadata
        │
        ▼
输出：处理后的 FIBITMAP*
```

也就是把现在 `VirtualScanner::preScanPrep()` 中的图像处理步骤改造成一个独立模块。

### 4.3 抽出前

```text
VirtualScanner
├── dib_
├── settings_
├── current_image_index_
├── scan_line_
├── acquireImage()
│   ├── 选择下一张图片
│   ├── FreeImage_Load()
│   └── preScanPrep()
│       ├── ensure24BitDib()
│       ├── applyPageSizeScaling()
│       ├── applyRotation()
│       ├── applyFlip()
│       └── applyPixelFormat()
```

图像处理逻辑和扫描状态管理混在同一个类中。

### 4.4 抽出后

```text
VirtualScanner
├── current_image_index_
├── scan_line_
├── acquireImage()
│   ├── 选择下一张图片
│   └── image_pipeline::BuildProcessedImage()
│       └── 返回 dib_ 给 TWAIN 输出

image_pipeline
├── LoadImage()
├── Ensure24Bit()
├── ApplyPageSizeScaling()
├── ApplyRotation()
├── ApplyFlip()
├── ApplyPixelFormat()
├── ApplyDpiMetadata()
└── BuildProcessedImage()
```

真实扫描：

```text
VirtualScanner::acquireImage()
        │
        ▼
image_pipeline::BuildProcessedImage(image_path, settings_)
        │
        ▼
dib_ 用于 TWAIN 输出
```

Settings UI Preview：

```text
SettingsServer /preview
        │
        ▼
image_pipeline::BuildProcessedImage(preview_image_path, preview_settings)
        │
        ▼
缩小成预览 PNG
        │
        ▼
返回给浏览器显示
```

### 4.5 关键接口示例

可以新增：

```text
src/image_pipeline.h
src/image_pipeline.cpp
```

接口示例：

```cpp
#ifndef IMAGE_PIPELINE_H_
#define IMAGE_PIPELINE_H_

#include <string>
#include "FreeImage.h"

struct ScannerSettings;

namespace image_pipeline {

FIBITMAP* BuildProcessedImage(
    const std::string& image_path,
    const ScannerSettings& settings);

FIBITMAP* BuildPreviewImage(
    const std::string& image_path,
    const ScannerSettings& settings,
    int max_width,
    int max_height);

}  // namespace image_pipeline

#endif
```

其中：

- `BuildProcessedImage()` 输出完整的扫描结果 bitmap。
- `BuildPreviewImage()` 可以在完整处理后再按预览框大小缩小，避免浏览器加载过大的图片。
- 返回的 `FIBITMAP*` 由调用方负责 `FreeImage_Unload()`。

### 4.6 函数改造示例

原来：

```cpp
bool VirtualScanner::applyRotation() {
  if (dib_ == nullptr) return false;
  int rot = settings_.rotation;
  ...
}
```

抽出后：

```cpp
bool ApplyRotation(FIBITMAP*& dib, int rotation) {
  if (dib == nullptr) return false;

  int rot = rotation;
  if (rot < 0 || rot > 3) rot = 0;
  if (rot == 0) return true;

  double angle = -static_cast<double>(rot) * 90.0;
  FIBITMAP* rotated = FreeImage_Rotate(dib, angle, nullptr);
  if (rotated == nullptr) return false;

  FreeImage_Unload(dib);
  dib = rotated;
  return true;
}
```

| 抽出前 | 抽出后 |
|---|---|
| 依赖 `VirtualScanner::dib_` | 通过参数传入 `FIBITMAP*& dib` |
| 依赖 `VirtualScanner::settings_` | 通过参数传入具体设置 |
| 只能由 `VirtualScanner` 使用 | `VirtualScanner` 和 `SettingsServer` 都可使用 |
| 容易影响扫描状态 | 无状态、低副作用 |

### 4.7 `VirtualScanner` 如何改用 pipeline

原来的 `acquireImage()` 流程：

```cpp
dib_ = FreeImage_Load(...);
if (!preScanPrep()) {
  return false;
}
```

改造后：

```cpp
dib_ = image_pipeline::BuildProcessedImage(image_path, settings_);
if (dib_ == nullptr) {
  return false;
}

calculateRowParams();
return true;
```

此后 `VirtualScanner` 主要负责扫描状态管理、选择下一张图片、读写 image index、TWAIN native/file transfer，以及 `scan_line_`、`row_offset_`、`dest_bytes_per_row_` 等传输相关状态。图像处理细节交给 `image_pipeline`。

### 4.8 Settings UI Preview 如何使用 pipeline

`SettingsServer` 增加 `/preview` endpoint 后，可以：

```cpp
ScannerSettings settings;
settings.pixel_type = ...;
settings.x_resolution = ...;
settings.y_resolution = ...;
settings.page_size = ...;
settings.page_fill_mode = ...;
settings.rotation = ...;
settings.flip = ...;

FIBITMAP* preview = image_pipeline::BuildPreviewImage(
    preview_image_path, settings, 320, 420);
if (preview == nullptr) {
  // 返回错误占位图或 HTTP 500
}
```

然后把 `preview` 保存成 PNG 或直接编码为 PNG response。

## 5. Preview 源图片选择

Preview 还需要定义“预览哪一张图”。因为真实扫描会按 index 轮询 `%APPDATA%\bntech\images` 中的图片，所以 Preview 不应该推进真实扫描 index。

可选方案：

### 5.1 取目录第一张图片

SettingsServer 自己扫描 `%APPDATA%\bntech\images`，按文件名排序后取第一张；没有图片时使用默认 `TWAIN_logo.png`。

优点：实现简单。缺点：不一定等于真实扫描下一张图片。

### 5.2 读取当前将要扫描的图片，但不推进 index

把图片选择逻辑抽出一个只读函数，例如：

```cpp
std::string GetCurrentPreviewImagePath();
```

行为：扫描图片目录，读取当前 image index，返回当前 index 对应图片；如果越界则返回第一张或默认图片；不调用 `saveImageIndex()`；不修改 `current_image_index_`。

优点：Preview 更接近真实下一次扫描。缺点：需要额外抽取图片索引逻辑。

### 5.3 允许用户选择 preview source

Settings UI 增加图片选择控件，让用户明确选择预览源图。

优点：最直观。缺点：UI 和状态管理复杂度增加，不适合第一版。

建议第一版使用 5.1 或 5.2。若强调与下一次扫描一致，选 5.2。

## 6. 推荐实施路线

### 6.1 如果目标是快速上线

推荐：

```text
第一版：方案一，纯前端示意 Preview
```

理由：改动最小，不影响扫描流程，不需要处理图片解码和 HTTP binary response，可以快速验证 UI 布局和用户体验。

### 6.2 如果目标是真实图片 Preview，但希望控制风险

推荐：

```text
第一版真实预览：方案五 + 方案四
```

也就是：Settings UI 增加 Preview 按钮；用户点击 Preview 时，后端生成临时 PNG；前端刷新 `<img>` 显示该 PNG。

理由：不需要每次设置变动都生成图片，性能和并发风险较低，临时 PNG 方便调试，可以先不做复杂的自动刷新和 debounce。

### 6.3 如果目标是长期维护和所见即所得

推荐：

```text
正式方案：方案六 + 方案三
```

也就是：

1. 抽出 `image_pipeline`。
2. 让 `VirtualScanner::acquireImage()` 改用 `image_pipeline::BuildProcessedImage()`。
3. SettingsServer 增加 `/preview` endpoint。
4. `/preview` 使用同一套 `image_pipeline` 生成缩略 PNG。
5. 前端对设置变化做 debounce 后自动刷新 preview。

理由：Preview 和实际扫描结果一致性最高；后续增加图像设置时，Scan 和 Preview 自动同步；避免复制图像处理代码；Preview 不污染真实扫描状态。

## 7. 建议结论

当前项目已经有较多图像处理设置：Page Size、Page Fill、Rotation、Flip、Pixel Type、DPI。Preview 如果只是前端模拟，很容易和真实扫描结果产生差异。

建议采用分阶段路线：

1. **短期 MVP**：先做纯前端示意 Preview，快速提供交互反馈。
2. **真实预览第一版**：增加 Preview 按钮，后端生成临时 PNG。
3. **长期正式版**：抽出 `VirtualScanner` image pipeline，Scan 和 `/preview` 共用同一套处理逻辑。

最终目标应是：

```text
Settings UI Preview 和真实 Scan 输出使用同一条 image_pipeline，
保证用户看到的预览与最终扫描结果尽可能一致。
```

</details>

<details>
<summary>English summary</summary>

This document compares several ways to implement Settings UI preview:

- frontend-only illustrative preview;
- frontend source-image preview with CSS/JS transforms;
- backend-generated `/preview` image;
- backend temporary preview file;
- manual Preview button;
- extracted `VirtualScanner` image-processing pipeline;
- full native UI or WebView2 UI.

The recommended long-term approach is to extract the current image processing flow from `VirtualScanner` into a stateless `image_pipeline` module. Both real scan and Settings UI preview should call the same `BuildProcessedImage(image_path, settings)` function. This keeps preview and final scan output consistent, avoids duplicating image-processing code, and prevents preview from mutating real scanner state such as image index, scan line, or active bitmap.

Recommended rollout:

1. Short-term MVP: frontend-only illustrative preview.
2. First real preview: manual Preview button + backend-generated temporary PNG.
3. Long-term final design: shared `image_pipeline` + `/preview` endpoint with debounce refresh.

</details>
