# BN Tech Virtual Scanner

**Author:** 葛宁 (Ning Ge) <tandge@gmail.com>

A TWAIN 2.5 compatible virtual flatbed scanner Data Source DLL for testing,
development, and scanner-application integration work.  It reads images from a
local folder, exposes them as scanner acquisitions, supports a lightweight
settings UI, and can transfer images through TWAIN native or file-transfer
modes.

<details>
<summary>中文说明 / Chinese</summary>

## 简介

BN Tech Virtual Scanner 是一个 TWAIN 2.5 虚拟平板扫描仪 Data Source DLL，
专为测试、开发和扫描应用集成场景设计。它会从本地图片目录读取图片，模拟真实扫描仪
逐张输出，并支持扫描时设置界面、Native Transfer 和 File Transfer。

核心功能：

- 模拟平板扫描仪，不支持 ADF / 双面扫描。
- 从 `%APPDATA%\bntech\images` 目录读取图片，按字母序轮转。
- 当图片目录为空时，回退使用安装目录中的 `TWAIN_logo.png`。
- 支持 Native Transfer，向应用返回 DIB 图像数据。
- 支持 File Transfer，输出 PNG、JPG、BMP、TIFF。
- 支持 `ShowUI=TRUE` 时弹出网页 settings UI。
- settings UI 可选择颜色模式、DPI、传输模式、文件格式、输出目录和文件名。
- 支持通过 `%APPDATA%\bntech\config.ini` 切换界面语言：`en_US` / `zh_CN`，默认 `en_US`。
- 使用 FreeImage 加载图片、转换像素格式、缩放分辨率。
- 扫描索引持久化到 `%APPDATA%\bntech\images\info.json`，跨 DLL 卸载保持进度。
- 输出文件和 Native Transfer 图像信息都会携带用户选择的 DPI。
- 同时构建 32 位和 64 位 TWAIN DS。

## 使用方式

### 准备测试图片

将测试图片放入：

```text
%APPDATA%\bntech\images\
```

支持格式：PNG、JPG、JPEG、BMP、TIF、TIFF。

文件按字母序排列，每次扫描自动前进到下一张。需要重置扫描顺序时，删除：

```text
%APPDATA%\bntech\images\info.json
```

### 在扫描应用中使用

在支持 TWAIN 的应用中选择：

```text
BN Tech Virtual Scanner
```

如果应用请求显示 UI，会打开本地网页 settings UI。点击 **Scan** 后开始扫描。

### 切换界面语言

界面语言从 `%APPDATA%\bntech\config.ini` 读取，未配置时默认使用 `en_US`：

```ini
language=zh_CN
```

可用值：`en_US`、`zh_CN`（也兼容 `lang` / `locale` 键名）。

在 XnView 的“扫描到...”流程中：

1. 在 XnView 的扫描到窗口中选择输出目录、文件名模式和文件格式。
2. 点击扫描。
3. 在弹出的 BN Tech settings UI 中选择颜色模式和 DPI，例如 600 DPI。
4. 点击 Scan。
5. XnView 生成的文件应保留用户选择的水平/垂直分辨率。

## DPI 元数据

项目会尽量确保扫描输出文件在 Windows 资源管理器“属性 → 详细信息”中显示正确的
水平分辨率和垂直分辨率。

已支持：

- PNG：写入或替换 `pHYs` chunk。
- JPG：写入或替换 JFIF APP0 density，单位为 dots per inch。
- BMP：写入 DIB header 的 `biXPelsPerMeter` / `biYPelsPerMeter`。
- TIFF：写入 `XResolution`、`YResolution`、`ResolutionUnit`。
- Native Transfer：`TW_IMAGEINFO.XResolution` / `YResolution` 使用 settings UI 中选择的 DPI。
- TWAIN 单位：声明 `ICAP_UNITS = TWUN_INCHES`，表示分辨率单位为 DPI/PPI。

注意：PNG 和 BMP 文件格式内部使用 pixels-per-meter 表示物理分辨率，代码会从 DPI
转换到 pixels-per-meter；Windows 会再换算并显示为 DPI。

## 支持的能力

| 能力 | 类型 | 容器 | 操作 | 默认值 | 可选值 |
|---|---|---|---|---|---|
| `CAP_SUPPORTEDCAPS` | UINT16 | ARRAY | GET | — | 其他能力 |
| `ICAP_XFERMECH` | UINT16 | ONEVALUE | 全部 | `TWSX_NATIVE` | `TWSX_NATIVE`, `TWSX_FILE` |
| `ICAP_PIXELTYPE` | UINT16 | ENUMERATION | 全部 | `TWPT_RGB` | `TWPT_BW`, `TWPT_GRAY`, `TWPT_RGB` |
| `ICAP_XRESOLUTION` | FIX32 | ENUMERATION | 全部 | `300` | `150`, `200`, `300`, `600` |
| `ICAP_YRESOLUTION` | FIX32 | ENUMERATION | 全部 | `300` | `150`, `200`, `300`, `600` |
| `ICAP_UNITS` | UINT16 | ONEVALUE | 全部 | `TWUN_INCHES` | `TWUN_INCHES` |
| `CAP_FEEDERENABLED` | BOOL | ONEVALUE | GET | `FALSE` | `FALSE` |
| `ICAP_PIXELFLAVOR` | UINT16 | ONEVALUE | 全部 | `TWPF_CHOCOLATE` | `TWPF_CHOCOLATE` |
| `ICAP_IMAGEFILEFORMAT` | UINT16 | ENUMERATION | 全部 | `TWFF_PNG` | `TWFF_TIFF`, `TWFF_BMP`, `TWFF_JFIF`, `TWFF_PNG` |
| `CAP_UICONTROLLABLE` | BOOL | ONEVALUE | GET | `TRUE` | `TRUE` |

“全部”操作 = GET + GETCURRENT + GETDEFAULT + SET + RESET。

## 编译和安装

### 环境要求

- CMake 3.15+
- Visual Studio 2022（含 C++ 桌面开发工作负载）
- FreeImage 库（已包含在 `pub/external` 中）
- WiX Toolset 4.0.4（仅生成 MSI 时需要）

### 推荐命令

不带参数运行会构建 win32 + win64，安装到 Windows TWAIN 目录，并生成 32/64 位 MSI 安装包：

```batch
build.bat
```

安装到 `C:\Windows` 需要管理员权限。脚本会检查权限并在需要时请求提权。

常用参数：

```batch
build.bat win32       rem 只构建 32 位，不安装
build.bat win64       rem 只构建 64 位，不安装
build.bat msi32       rem 构建 32 位 MSI，不安装
build.bat msi64       rem 构建 64 位 MSI，不安装
build.bat install     rem 只安装已有构建产物
build.bat clean       rem 清理 build\win32 和 build\win64
build.bat debug       rem Debug 构建，默认仍为 Release
```

安装目录：

- 32 位：`C:\Windows\twain_32\bntech\`
- 64 位：`C:\Windows\twain_64\bntech\`

安装文件包括：

1. `bntech_virtual_scanner.ds`
2. `FreeImage.dll`
3. `TWAIN_logo.png`

如果安装时提示 `.ds` 文件被占用，请关闭 XnView、Twack 或其他扫描应用后重试。
TWAIN DS 本质上是 DLL，被应用加载时无法覆盖。

MSI 输出位置：

```text
build\installer\win32\bntech_virtual_scanner_win32.msi
build\installer\win64\bntech_virtual_scanner_win64.msi
```

MSI 默认使用英文配置。需要中文配置时，可在安装时传入：

```batch
msiexec /i build\installer\win64\bntech_virtual_scanner_win64.msi APP_LANGUAGE=zh_CN
```

MSI 安装和卸载完成后会弹出成功提示；如果安装或卸载失败并触发回滚，会弹出失败提示和常见可能原因。

## 项目结构

```text
src/
├── capability.h / .cpp        TWAIN 能力协商。
├── twain_data_source.h/.cpp   DS 状态机、消息路由、Native/File Transfer、DIB 构建。
├── settings_server.h/.cpp     本地网页 settings UI 和 HTTP server。
├── ds_entry.cpp               DLL 入口，导出 DS_Entry。
├── virtual_scanner.h/.cpp     图片加载、DPI 缩放、像素转换、DPI 元数据写入。
├── unit_convert.h/.cpp        TWAIN FIX32 转换工具。
├── platform.h                 Windows 平台宏，CMake 使用 /FI 强制包含。
└── resource.h                 资源 ID。
```

更多变更记录见 [`CHANGELOG.md`](CHANGELOG.md)。

</details>

## Features

- **Virtual flatbed scanner**: Presents as a basic TWAIN flatbed scanner with
  no ADF or duplex support.
- **Folder-backed image source**: Reads images from `%APPDATA%\bntech\images`,
  sorted alphabetically, and advances on each acquisition.
- **Default fallback image**: Uses `TWAIN_logo.png` from the installation
  directory when the image folder is empty.
- **Settings UI**: Shows a lightweight local web UI when the application sets
  `ShowUI=TRUE`.
- **Localization**: Supports `en_US` and `zh_CN` UI strings via
  `%APPDATA%\bntech\config.ini`; defaults to `en_US`.
- **Scan-time options**: Supports color mode, DPI, transfer mode, file format,
  output directory, and output filename.
- **Native transfer**: Returns DIB image data through `DAT_IMAGENATIVEXFER`.
- **File transfer**: Supports `DAT_SETUPFILEXFER` / `DAT_IMAGEFILEXFER` for
  PNG, JPG, BMP, and TIFF output.
- **DPI metadata**: Writes horizontal and vertical resolution metadata so
  Windows Explorer and image applications can see the selected DPI.
- **Image conversion**: Uses FreeImage for loading, DPI-based scaling, BW,
  grayscale, and RGB conversion.
- **Index persistence**: Stores the next-image index in
  `%APPDATA%\bntech\images\info.json`.
- **Dual architecture**: Builds and installs both 32-bit and 64-bit TWAIN data
  sources.

## Usage

### Image source folder

Place test images in:

```text
%APPDATA%\bntech\images\
```

Supported input formats: PNG, JPG, JPEG, BMP, TIF, TIFF.

Images are sorted alphabetically.  Each scan advances to the next image.  To
reset the order, delete:

```text
%APPDATA%\bntech\images\info.json
```

### In a TWAIN application

Select the source named:

```text
BN Tech Virtual Scanner
```

If the application requests UI, the data source opens a local browser-based
settings page.  Press **Scan** to continue the acquisition.

### UI language

The data source reads the UI language from `%APPDATA%\bntech\config.ini`.  If the
file or setting is missing, it defaults to `en_US`:

```ini
language=zh_CN
```

Supported values: `en_US`, `zh_CN` (`lang` and `locale` are accepted as aliases
for the key name).

For XnView "Scan to...":

1. Choose output directory, filename pattern, and output format in XnView.
2. Click Scan.
3. In the BN Tech settings UI, select the desired DPI, for example 600 DPI.
4. Click Scan.
5. The generated file should retain the selected horizontal and vertical DPI.

## DPI metadata behavior

The scanner writes or propagates DPI metadata for both direct TWAIN file
transfer and native-transfer workflows where the application saves the final
file.

Supported output metadata:

- PNG: `pHYs` chunk.
- JPG: JFIF APP0 density fields with dots-per-inch units.
- BMP: DIB header `biXPelsPerMeter` and `biYPelsPerMeter`.
- TIFF: `XResolution`, `YResolution`, and `ResolutionUnit` tags.
- Native transfer: `TW_IMAGEINFO.XResolution` and `YResolution` report the DPI
  selected in the settings UI.
- TWAIN units: `ICAP_UNITS = TWUN_INCHES`.

PNG and BMP store their physical resolution internally as pixels per meter; the
scanner converts from DPI to pixels per meter, and Windows converts it back for
Explorer's Details tab.

## Supported capabilities

| Capability | Type | Container | Ops | Default | Values |
|---|---|---|---|---|---|
| `CAP_SUPPORTEDCAPS` | UINT16 | ARRAY | GET | — | Other supported capabilities |
| `ICAP_XFERMECH` | UINT16 | ONEVALUE | All | `TWSX_NATIVE` | `TWSX_NATIVE`, `TWSX_FILE` |
| `ICAP_PIXELTYPE` | UINT16 | ENUMERATION | All | `TWPT_RGB` | `TWPT_BW`, `TWPT_GRAY`, `TWPT_RGB` |
| `ICAP_XRESOLUTION` | FIX32 | ENUMERATION | All | `300` | `150`, `200`, `300`, `600` |
| `ICAP_YRESOLUTION` | FIX32 | ENUMERATION | All | `300` | `150`, `200`, `300`, `600` |
| `ICAP_UNITS` | UINT16 | ONEVALUE | All | `TWUN_INCHES` | `TWUN_INCHES` |
| `CAP_FEEDERENABLED` | BOOL | ONEVALUE | GET | `FALSE` | `FALSE` |
| `ICAP_PIXELFLAVOR` | UINT16 | ONEVALUE | All | `TWPF_CHOCOLATE` | `TWPF_CHOCOLATE` |
| `ICAP_IMAGEFILEFORMAT` | UINT16 | ENUMERATION | All | `TWFF_PNG` | `TWFF_TIFF`, `TWFF_BMP`, `TWFF_JFIF`, `TWFF_PNG` |
| `CAP_UICONTROLLABLE` | BOOL | ONEVALUE | GET | `TRUE` | `TRUE` |

"All" operations = GET + GETCURRENT + GETDEFAULT + SET + RESET.

## Building and installation

### Prerequisites

- CMake 3.15+
- Visual Studio 2022 with the C++ desktop development workload.
- FreeImage library, included under `pub/external`.
- WiX Toolset 4.0.4, required only when generating MSI packages.

### Recommended command

Run without arguments to build win32 + win64, install both data sources, and generate both MSI packages:

```batch
build.bat
```

Installing to `C:\Windows` requires administrator privileges.  The script checks
for elevation and requests it when needed.

Common commands:

```batch
build.bat win32       rem Build 32-bit only; do not install.
build.bat win64       rem Build 64-bit only; do not install.
build.bat msi32       rem Build the 32-bit MSI; do not install.
build.bat msi64       rem Build the 64-bit MSI; do not install.
build.bat install     rem Install existing build outputs only.
build.bat clean       rem Remove build\win32 and build\win64.
build.bat debug       rem Debug build; Release is the default.
```

The TWAIN data source is installed to:

- **32-bit**: `C:\Windows\twain_32\bntech\`
- **64-bit**: `C:\Windows\twain_64\bntech\`

Installed files:

1. `bntech_virtual_scanner.ds`
2. `FreeImage.dll`
3. `TWAIN_logo.png`

If installation fails because the `.ds` file is in use, close XnView, Twack, or
any other scanning application and run the install again.  A TWAIN `.ds` file is
a DLL and cannot be overwritten while loaded.

### Manual CMake build

```batch
cmake -S . -B build\win32 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build\win32
cmake --install build\win32 --prefix C:/Windows

cmake -S . -B build\win64 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build\win64
cmake --install build\win64 --prefix C:/Windows
```

## Architecture

```text
src/
├── capability.h / .cpp        TWAIN capability negotiation.
├── twain_data_source.h/.cpp   DS state machine, message routing,
│                               native/file transfer, and DIB construction.
├── settings_server.h/.cpp     Local web settings UI and HTTP server.
├── ds_entry.cpp               DLL entry point and exported DS_Entry.
├── virtual_scanner.h/.cpp     Image loading, DPI scaling, pixel conversion,
│                               strip output, and DPI metadata patching.
├── unit_convert.h/.cpp        TWAIN FIX32 conversion helpers.
├── platform.h                 Windows platform macros, force-included by CMake.
└── resource.h                 Resource IDs.
```

The DSM interface helpers for loading `TWAIN_32.dll`, memory management, and
application event notification are implemented as file-scope functions in
`twain_data_source.cpp`.

## Changelog

See [`CHANGELOG.md`](CHANGELOG.md) for notable changes.

## Code style

The project follows [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html):

- Use `//` line comments with sentence capitalization and periods.
- Keep one statement per line; avoid multi-statement compression.
- Use proper indentation, spacing, and brace placement.
- Use `snake_case` for variables and `CamelCase` for classes/functions.
- Do not use `using namespace std`; prefer `static_cast` over C-style casts.
