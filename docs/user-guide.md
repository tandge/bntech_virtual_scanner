# BN Tech Virtual Scanner — User Guide

End-user usage guide for the BN Tech Virtual Scanner TWAIN 2.5 Data Source.

<details open>
<summary>中文说明</summary>

## 1. 简介

BN Tech Virtual Scanner 是一个 TWAIN 2.5 虚拟平板扫描仪 (Data Source DLL)，
不需要真实硬件，从本地图片目录读取图片，模拟真实扫描仪逐张输出。

适用场景：

- TWAIN 应用开发与回归测试
- 扫描类软件集成、安装与售后演示
- 教学和文档截图
- 自动化 UI 测试（固定输出图片即固定扫描结果）

主要能力：

- 32 位 + 64 位双 TWAIN DS
- 三种传输模式：Native (DIB 句柄)、File (PNG/JPG/BMP/TIFF)、Memory (条带)
- 三种像素格式：彩色 (24-bit RGB)、灰度 (8-bit)、黑白 (1-bit)
- 四种 DPI：150 / 200 / 300 / 600
- 四种页面尺寸：US Letter / US Legal / A4 / A5
- 支持 ShowUI=TRUE 弹出本地网页式设置界面
- 中英文双语界面，由 `config.ini` 控制
- 输出文件携带正确的 DPI 元数据 (PNG/JPG/BMP/TIFF 都已实现)
- 扫描索引持久化，跨进程重启不丢失进度
- MSI 安装包 (32 位 / 64 位)，自动按系统语言选择中英文向导

## 2. 系统要求

- Windows 10 / 11 (x86 或 x64)
- 已安装 TWAIN DSM (大多数 Windows 自带；若没有，可装 [TWAIN Working Group DSM](https://github.com/twain/twain-dsm))
- 默认浏览器 (用于打开本地 settings UI 页面)
- 用户拥有 `%APPDATA%\bntech\` 读写权限

## 3. 安装

### 3.1 使用 MSI 安装包（推荐）

1. 下载与系统位数匹配的安装包：
   - 64 位 Windows: `bntech_virtual_scanner_win64.msi`
   - 32 位 Windows: `bntech_virtual_scanner_win32.msi`
   - 同时安装也是常见做法（一些 32 位应用如 Twack 32 必须使用 32 位 DS，64 位应用如 NAPS2 64 必须使用 64 位 DS）。
2. 双击 MSI；安装向导会自动按系统语言显示中文或英文 UI。
3. 按提示选择安装目录（默认 `C:\Windows\twain_64\bntech\` 或 `C:\Windows\twain_32\bntech\`）。
4. 安装完成后会自动在 Windows TWAIN 目录里注册 `.ds` 文件，TWAIN 应用即可识别。

中文系统会自动在 `%APPDATA%\bntech\config.ini` 写入 `language=zh_CN`，
英文系统默认 `en_US`。如需强制安装某一种语言界面：

```batch
msiexec /i bntech_virtual_scanner_win64.msi TRANSFORMS=:2052   rem 简体中文
msiexec /i bntech_virtual_scanner_win64.msi TRANSFORMS=:1033   rem 英文
```

### 3.2 手动安装

把以下三个文件复制到 TWAIN 目录：

| 位数 | 目标目录 |
|---|---|
| 32 位 | `C:\Windows\twain_32\bntech\` |
| 64 位 | `C:\Windows\twain_64\bntech\` |

文件：

1. `bntech_virtual_scanner.ds`
2. `FreeImage.dll`
3. `TWAIN_logo.png` （图片目录为空时的回退图）

复制需要管理员权限。

### 3.3 卸载

通过 Windows "应用和功能" → "BN Tech Virtual Scanner" 卸载，或：

```batch
msiexec /x bntech_virtual_scanner_win64.msi
```

卸载不会删除 `%APPDATA%\bntech\` 目录及里面的图片、配置和扫描索引，如需彻底清理请手动删除。

## 4. 准备测试图片

把要"被扫描"的图片放入：

```text
%APPDATA%\bntech\images\
```

- 在文件资源管理器地址栏直接粘贴上面这一行即可打开；首次安装该目录不存在时请手动创建。
- 支持格式：PNG / JPG / JPEG / BMP / TIF / TIFF。
- 文件按名称（不区分大小写）字母序排序，每次扫描自动前进到下一张，循环遍历。
- 当目录为空，DS 会回退到安装目录里的 `TWAIN_logo.png`。

重置扫描顺序：

```text
del %APPDATA%\bntech\images\info.json
```

下次扫描会从字母序第一张重新开始。

建议：起名时用数字前缀控制顺序，例如 `01_color_photo.png` / `02_text_grayscale.png` / `03_bw_invoice.png`，便于回归测试。

## 5. 在 TWAIN 应用中扫描

### 5.1 选择数据源

在你的 TWAIN 应用中找到"选择源"或"Select Source"菜单，在弹出列表中选择：

```text
BN Tech Virtual Scanner
```

如果同时安装了 32 位和 64 位，列表里可能各出现一次（取决于应用位数）。32 位应用只能看到 32 位 DS，64 位应用只能看到 64 位 DS，这是 TWAIN 框架的限制。

### 5.2 弹出 settings UI 时

如果应用以 `ShowUI=TRUE` 启动扫描，DS 会打开本地浏览器页面（默认浏览器），上面有可调参数：

| 字段 | 选项 | 说明 |
|---|---|---|
| 颜色模式 | Black & White / Grayscale / Color | 对应 1-bit / 8-bit / 24-bit |
| 分辨率 | 150 / 200 / 300 / 600 DPI | 同时改变像素尺寸和元数据 |
| 页面尺寸 | US Letter / US Legal / A4 / A5 | 决定输出像素宽高 |
| 传输模式 | Native (memory) / File | 仅在应用未指定文件输出时显示 |
| 文件格式 | PNG / JPG / BMP / TIFF | 仅 File 模式可见 |
| 输出目录 | 任意路径 | 仅 File 模式可见 |
| 输出文件名 | 字符串 | 仅 File 模式可见 |

操作：

- **Scan**：使用当前设置开始扫描，DS 把图片交还给应用。
- **Cancel**：取消，应用收到 `TWRC_CANCEL`。

注意：

- 当宿主应用已经通过 `DAT_SETUPFILEXFER` 指定了文件输出路径（如 XnView 的"扫描到..."流程），DS 会自动隐藏文件相关字段，避免与应用决定的输出位置冲突。
- 部分应用会以 `ShowUI=FALSE` 直接驱动扫描，此时不显示 settings UI，DS 使用应用预先设置的 `ICAP_PIXELTYPE / ICAP_XRESOLUTION / ICAP_YRESOLUTION / ICAP_XFERMECH`。

### 5.3 一次完整扫描示例 (XnView)

1. 打开 XnView → File → Acquire → Select Source... → BN Tech Virtual Scanner。
2. File → Acquire → 启动扫描。
3. 弹出 settings UI。选择 Color / 300 DPI / A4 / File / PNG。
4. 点击 Scan。
5. XnView 收到图像并显示，输出文件保留 300 DPI 元数据。

### 5.4 "扫描到..." (Scan to...) 场景

XnView / NAPS2 / Picasa 等支持"扫描到指定目录"。它们会通过 `DAT_SETUPFILEXFER` 把目标文件路径告诉 DS：

1. 在宿主应用里选择输出目录和文件名模式（如 `scan_{date}_{n}.png`）。
2. 点击扫描。
3. settings UI 弹出时只显示颜色 / DPI / 页面尺寸（不再显示文件相关字段）。
4. 选择并点击 Scan。
5. 文件最终落在宿主应用指定的位置。

## 6. 设置界面语言

DS 启动时读取 `%APPDATA%\bntech\config.ini`：

```ini
language=zh_CN
```

- 可用值：`en_US`（默认）、`zh_CN`
- 兼容键名：`language` / `lang` / `locale`

文件不存在或键缺失时使用 `en_US`。中文系统 MSI 安装会自动写入 `language=zh_CN`。

## 7. 传输模式选择

下表帮助选择最合适的传输模式：

| 模式 | 何时选 | 缺点 |
|---|---|---|
| **Native** (DIB) | 默认；应用希望以 BMP/DIB 直接拿到内存数据 | A4 600 DPI 24-bit 占约 100 MB 内存 |
| **File** | 应用希望 DS 直接保存文件（PNG / JPG / BMP / TIFF） | 多一次磁盘 I/O |
| **Memory** | 应用希望按条带获取，便于流式处理或实时显示 | 仍受 DS 全图加载限制 |

Native / Memory 在 settings UI 里都对应 "Native (memory)" 选项；具体使用哪一种由宿主应用通过 `ICAP_XFERMECH` 选择，DS 完全兼容。

## 8. DPI 与文件元数据

DS 严格保证输出文件携带 UI 上选定的 DPI：

| 输出 | 元数据载体 |
|---|---|
| PNG | `pHYs` chunk |
| JPG | JFIF APP0 density (dots per inch) |
| BMP | DIB header `biXPelsPerMeter` / `biYPelsPerMeter` |
| TIFF | `XResolution` / `YResolution` / `ResolutionUnit` |
| Native / Memory | `TW_IMAGEINFO.XResolution` / `YResolution` |
| TWAIN | `ICAP_UNITS = TWUN_INCHES` |

在 Windows 资源管理器中右键 → 属性 → 详细信息，可以看到"水平分辨率 / 垂直分辨率"显示为所选 DPI。

## 9. 常见问题

### Q1. TWAIN 应用列表里看不到 BN Tech Virtual Scanner

- 32 位应用必须装 32 位 DS；64 位应用必须装 64 位 DS。
- 确认 `C:\Windows\twain_32\bntech\bntech_virtual_scanner.ds` 或 `C:\Windows\twain_64\bntech\bntech_virtual_scanner.ds` 存在。
- 部分应用缓存了源列表，重启应用即可。

### Q2. 扫描出来颜色不正确（红蓝互换）

- 此问题已在 Memory Transfer 模式中修正：确认你使用的是最新版本 DLL；若仍发生请提交 issue。

### Q3. settings UI 没弹出

- 应用可能用 `ShowUI=FALSE` 直接扫描，这是正常行为。
- 检查默认浏览器是否能正常启动；DS 用本机 HTTP 端口提供 UI。
- 确认 `TWAIN_logo.png` / `FreeImage.dll` 与 `.ds` 同目录，UI 进程缺组件会启动失败。

### Q4. 扫描结果一直是同一张图

- DS 按字母序自动前进。若只有一张图，自然每次都一样。
- 删除 `%APPDATA%\bntech\images\info.json` 或加更多图片。

### Q5. 安装报"文件正在被使用"

- 关闭所有正在使用扫描 DS 的应用（XnView / Twack 32 / NAPS2 / Photoshop 等）后重试。
- TWAIN DS 本质是 DLL，被应用 LoadLibrary 后无法覆盖。

### Q6. 想强制切换 UI 语言

- 编辑 `%APPDATA%\bntech\config.ini`，设置 `language=en_US` 或 `language=zh_CN`，然后重新启动应用。

### Q7. 大图扫描应用崩溃

- A4 600 DPI 24-bit 单图约 100 MB，加上 DIB 副本可达 200 MB。32 位应用受 2 GB 地址空间限制，过大图时请改用 300 DPI 或 Gray。

### Q8. 想验证 32 位 / 64 位是否都装好

- 64 位测试：用 IrfanView 64 (官网下载) 或 NAPS2 64 → File → Acquire → 看是否有源。
- 32 位测试：用 Twack 32 / XnView (32 位) → 同样路径。

## 10. 已知限制

- 单页虚拟平板，不支持 ADF / 双面 / 多页一次扫描。
- 不支持 16/48-bit 高位深像素。
- File 模式不支持 PDF 输出（输出格式限于 PNG / JPG / BMP / TIFF）。
- Memory 模式不支持压缩条带 (`TWCP_NONE` only)。
- 单图最大尺寸受系统可用内存限制，没有"流式"低内存模式。
- 仅 Windows 平台 (无 macOS / Linux DS)。
- 详细设计细节见 [开发技术 / Dev Logs](devlog.md)。

## 11. 反馈与支持

- 问题反馈：在项目仓库提交 issue，并附上：
  - Windows 版本
  - 宿主应用名 + 版本
  - DS 位数 (32 / 64)
  - settings UI 截图或 `%APPDATA%\bntech\` 下相关文件
- 邮箱：tandge@gmail.com

</details>

<details>
<summary>English</summary>

## 1. Introduction

BN Tech Virtual Scanner is a TWAIN 2.5 virtual flatbed scanner (Data Source DLL) that requires no real hardware. It reads images from a local folder and presents them to TWAIN applications as if they were freshly scanned pages.

Typical use cases:

- TWAIN application development and regression testing
- Scanner-software integration, installation, and customer demos
- Teaching and documentation screenshots
- Automated UI testing (fixed input image = deterministic scan output)

Key capabilities:

- 32-bit and 64-bit TWAIN DS
- Three transfer modes: Native (DIB handle), File (PNG/JPG/BMP/TIFF), Memory (strip)
- Three pixel formats: Color (24-bit RGB), Grayscale (8-bit), Black & White (1-bit)
- Four DPIs: 150 / 200 / 300 / 600
- Four page sizes: US Letter / US Legal / A4 / A5
- Local browser-based settings UI on `ShowUI=TRUE`
- English and Simplified Chinese UI, switchable via `config.ini`
- Output files carry correct DPI metadata (PNG / JPG / BMP / TIFF)
- Scan index persists across processes
- MSI installer (32 / 64), wizard language auto-selected by Windows locale

## 2. System Requirements

- Windows 10 or 11 (x86 or x64)
- TWAIN DSM installed (bundled with most Windows; otherwise install the [TWAIN Working Group DSM](https://github.com/twain/twain-dsm))
- A default web browser (used to open the local settings UI)
- Read/write access to `%APPDATA%\bntech\`

## 3. Installation

### 3.1 With the MSI installer (recommended)

1. Download the package matching your Windows bitness:
   - 64-bit Windows: `bntech_virtual_scanner_win64.msi`
   - 32-bit Windows: `bntech_virtual_scanner_win32.msi`
   - Installing both is common: 32-bit apps such as Twack 32 require the 32-bit DS; 64-bit apps such as NAPS2 64 require the 64-bit DS.
2. Double-click the MSI. The wizard auto-detects the OS language and shows English or Chinese UI accordingly.
3. Accept (or change) the install location (default `C:\Windows\twain_64\bntech\` or `C:\Windows\twain_32\bntech\`).
4. After installation the `.ds` file is in the Windows TWAIN folder and any TWAIN app can discover the scanner.

To force a specific UI language:

```batch
msiexec /i bntech_virtual_scanner_win64.msi TRANSFORMS=:2052   rem Simplified Chinese
msiexec /i bntech_virtual_scanner_win64.msi TRANSFORMS=:1033   rem English
```

### 3.2 Manual install

Copy these three files into the TWAIN directory (admin required):

| Bitness | Target folder |
|---|---|
| 32-bit | `C:\Windows\twain_32\bntech\` |
| 64-bit | `C:\Windows\twain_64\bntech\` |

Files:

1. `bntech_virtual_scanner.ds`
2. `FreeImage.dll`
3. `TWAIN_logo.png` (fallback image when the image folder is empty)

### 3.3 Uninstall

Use Windows "Apps & features" → "BN Tech Virtual Scanner", or:

```batch
msiexec /x bntech_virtual_scanner_win64.msi
```

Uninstall does not remove `%APPDATA%\bntech\` (images, config, scan index). Delete that folder manually for a clean wipe.

## 4. Preparing input images

Drop the images you want to be "scanned" into:

```text
%APPDATA%\bntech\images\
```

- Paste the path above into Explorer's address bar to open it. If the folder does not exist on first install, create it.
- Supported formats: PNG / JPG / JPEG / BMP / TIF / TIFF.
- Files are sorted alphabetically (case-insensitive). Each scan advances to the next image and wraps around.
- If the folder is empty the DS falls back to `TWAIN_logo.png` from the install folder.

Reset the scan order:

```text
del %APPDATA%\bntech\images\info.json
```

The next scan restarts from the first file.

Tip: prefix names with numbers (e.g. `01_color_photo.png`, `02_text_grayscale.png`, `03_bw_invoice.png`) to control the order for regression tests.

## 5. Scanning from a TWAIN application

### 5.1 Pick the source

In your TWAIN app open "Select Source" (sometimes "Choose Device") and pick:

```text
BN Tech Virtual Scanner
```

If both bitnesses are installed it may appear once per process bitness. 32-bit apps see only the 32-bit DS and vice versa; this is a TWAIN framework limitation.

### 5.2 When the settings UI pops up

If the app starts the scan with `ShowUI=TRUE`, the DS opens a local web page in your default browser:

| Field | Choices | Notes |
|---|---|---|
| Color mode | Black & White / Grayscale / Color | Maps to 1-bit / 8-bit / 24-bit |
| Resolution | 150 / 200 / 300 / 600 DPI | Also drives output pixel size and metadata |
| Page size | US Letter / US Legal / A4 / A5 | Determines output pixel width/height |
| Transfer mode | Native (memory) / File | Hidden when the app already set a file destination |
| File format | PNG / JPG / BMP / TIFF | File mode only |
| Output directory | Any path | File mode only |
| Output filename | String | File mode only |

Buttons:

- **Scan** — start scanning with the current settings.
- **Cancel** — abort; the app receives `TWRC_CANCEL`.

Notes:

- If the host app already set a file destination via `DAT_SETUPFILEXFER` (XnView's "Scan to..." flow does this), file-related fields are hidden to avoid conflict.
- Many apps drive the DS with `ShowUI=FALSE`. In that case the UI does not appear and the DS uses values pre-set via `ICAP_PIXELTYPE / ICAP_XRESOLUTION / ICAP_YRESOLUTION / ICAP_XFERMECH`.

### 5.3 End-to-end example (XnView)

1. XnView → File → Acquire → Select Source... → BN Tech Virtual Scanner.
2. File → Acquire → start scanning.
3. Settings UI opens. Pick Color / 300 DPI / A4 / File / PNG.
4. Click Scan.
5. XnView receives the image and displays it; the saved file carries 300 DPI metadata.

### 5.4 "Scan to..." workflow

Apps like XnView / NAPS2 / Picasa support "Scan to a directory". They pass the target path via `DAT_SETUPFILEXFER`:

1. Choose output folder and filename pattern (e.g. `scan_{date}_{n}.png`) in the host app.
2. Click scan.
3. The settings UI shows only color / DPI / page size (file-related fields hidden).
4. Pick the desired options and click Scan.
5. The file is saved where the host app asked.

## 6. UI language

The DS reads `%APPDATA%\bntech\config.ini` on startup:

```ini
language=zh_CN
```

- Allowed values: `en_US` (default), `zh_CN`
- Aliases for the key: `language` / `lang` / `locale`

If the file or key is missing, `en_US` is used. The Chinese MSI auto-writes `language=zh_CN` during install.

## 7. Choosing a transfer mode

| Mode | When to use | Cost |
|---|---|---|
| **Native** (DIB) | Default; app wants a single in-memory DIB | A4 600 DPI 24-bit ≈ 100 MB RAM |
| **File** | App wants the DS to save the file directly (PNG/JPG/BMP/TIFF) | Extra disk I/O |
| **Memory** | App wants per-strip delivery for streaming / live preview | Whole image still kept in DS memory |

Native and Memory share the "Native (memory)" choice in the settings UI; the actual mech is whatever the host app sets via `ICAP_XFERMECH`. All three modes are fully supported.

## 8. DPI and file metadata

The scanner guarantees that the DPI you pick in the UI ends up in the output:

| Output | Where the DPI lives |
|---|---|
| PNG | `pHYs` chunk |
| JPG | JFIF APP0 density (dots per inch) |
| BMP | DIB header `biXPelsPerMeter` / `biYPelsPerMeter` |
| TIFF | `XResolution` / `YResolution` / `ResolutionUnit` |
| Native / Memory | `TW_IMAGEINFO.XResolution` / `YResolution` |
| TWAIN | `ICAP_UNITS = TWUN_INCHES` |

Right-click a saved file in Windows Explorer → Properties → Details to see horizontal/vertical resolution.

## 9. FAQ

### Q1. The TWAIN source list does not show BN Tech Virtual Scanner

- 32-bit apps require the 32-bit DS; 64-bit apps require the 64-bit DS.
- Verify `C:\Windows\twain_32\bntech\bntech_virtual_scanner.ds` (or the 64-bit equivalent) exists.
- Some apps cache the source list; restart the app.

### Q2. Scan output has swapped red/blue channels

- This was fixed for Memory Transfer mode. Make sure you are running the latest DLL; file an issue if it still happens.

### Q3. The settings UI does not appear

- The host app may have started with `ShowUI=FALSE`; this is normal.
- Verify the default browser launches; the DS serves the UI on a local HTTP port.
- Make sure `TWAIN_logo.png` and `FreeImage.dll` are in the same folder as the `.ds` — missing components prevent UI startup.

### Q4. Every scan returns the same image

- The DS auto-advances alphabetically. With only one image the result repeats.
- Delete `%APPDATA%\bntech\images\info.json` or add more images.

### Q5. Install says "file in use"

- Close every app that has loaded the DS (XnView / Twack 32 / NAPS2 / Photoshop, …) and retry.
- A TWAIN DS is a DLL; it cannot be overwritten while loaded.

### Q6. How do I force a UI language?

- Edit `%APPDATA%\bntech\config.ini` to set `language=en_US` or `language=zh_CN`, then restart the host app.

### Q7. Host app crashes on large scans

- A4 at 600 DPI 24-bit is ~100 MB per image, plus the DIB copy. 32-bit apps hit the 2 GB address-space wall; drop to 300 DPI or use Gray for very large scans.

### Q8. How do I verify both 32-bit and 64-bit work?

- 64-bit: use IrfanView 64 or NAPS2 64 → File → Acquire → check the source list.
- 32-bit: use Twack 32 or XnView (32-bit) → same path.

## 10. Known limitations

- Single-page virtual flatbed; no ADF, duplex, or multi-page-per-scan.
- No 16/48-bit pixel formats.
- File mode does not output PDF (PNG/JPG/BMP/TIFF only).
- Memory mode does not deliver compressed strips (`TWCP_NONE` only).
- Maximum image size is limited by available RAM; there is no low-memory streaming mode.
- Windows only (no macOS or Linux DS).
- See [Dev Logs](devlog.md) for detailed design notes.

## 11. Feedback and support

- File issues on the project's repository with:
  - Windows version
  - Host app name + version
  - DS bitness (32 / 64)
  - Settings UI screenshot or files under `%APPDATA%\bntech\`
- Email: tandge@gmail.com

</details>
