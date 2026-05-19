# BN Tech Virtual Scanner

**Author:** 葛宁 (Ning Ge) \<tandge@gmail.com\>

<details>
<summary>中文 / Click for Chinese</summary>

## 简介

BN Tech Virtual Scanner 是一个最小化的 TWAIN 2.5 虚拟平板扫描仪 Data Source DLL，
专为测试和开发场景设计。使用最少的能力集即可被标准 TWAIN 应用程序（Twack32、
Windows 传真和扫描）识别并使用。

核心功能：

- 模拟平板扫描仪，不支持 ADF / 双面扫描
- 从 `%APPDATA%\bntech\images` 目录读取图片，按字母序轮转
- 仅支持原生传输（Native Transfer），以 DIB 格式输出图像数据
- 无设置界面，`ShowUI=TRUE` 被接受但不弹出对话框
- 同时构建 32 位和 64 位 DLL
- 使用 FreeImage 库加载图像并进行像素类型转换
- 扫描索引持久化到 `info.json`，跨 DLL 卸载保持进度

## 支持的能力

仅注册 8 个能力 — 平板扫描仪兼容所需的最小集：

| 能力 | 类型 | 容器 | 操作 | 默认值 | 可选值 |
|---|---|---|---|---|---|
| `CAP_SUPPORTEDCAPS` | UINT16 | ARRAY | GET | — | 其他 7 个能力 |
| `ICAP_XFERMECH` | UINT16 | ONEVALUE | 全部 | TWSX_NATIVE | TWSX_NATIVE |
| `ICAP_PIXELTYPE` | UINT16 | ENUMERATION | 全部 | TWPT_RGB | BW, GRAY, RGB |
| `ICAP_XRESOLUTION` | FIX32 | ENUMERATION | 全部 | 300 | 150, 200, 300, 600 |
| `ICAP_YRESOLUTION` | FIX32 | ENUMERATION | 全部 | 300 | 150, 200, 300, 600 |
| `CAP_FEEDERENABLED` | BOOL | ONEVALUE | GET | FALSE | FALSE |
| `ICAP_PIXELFLAVOR` | UINT16 | ONEVALUE | 全部 | TWPF_CHOCOLATE | TWPF_CHOCOLATE |
| `CAP_UICONTROLLABLE` | BOOL | ONEVALUE | GET | TRUE | TRUE |

"全部"操作 = GET + GETCURRENT + GETDEFAULT + SET + RESET。

## 项目结构

```
src/
├── capability.h / .cpp      能力协商（8 个能力）
├── twain_data_source.h/.cpp  DS 状态机、消息路由、原生传输、
│                             DIB 构建、内联 DSM 接口
├── ds_entry.cpp              DLL 入口（DS_Entry + DllMain）
├── virtual_scanner.h / .cpp  图像加载、像素转换、逐行扫描输出
├── unit_convert.h / .cpp     TWAIN 单位系统和 FIX32 工具函数
├── platform.h                Windows 平台宏（/FI 强制包含）
├── resource.h                资源 ID
├── modules.md                模块说明文档
└── core.md                   编码规范
```

DSM 接口（TWAIN_32.dll 加载、事件通知、内存管理）直接内联到
`twain_data_source.cpp` 中，作为文件作用域的静态函数 — 无独立模块。

## 编译构建

### 环境要求

- CMake 3.15+
- Visual Studio 2022（含 C++ 桌面开发工作负载）
- FreeImage 库（已包含在 `pub/external` 中）

### 快速构建

```batch
build.bat win32
build.bat win64
```

### 手动构建

```batch
mkdir build\win32 && cd build\win32
cmake ..\.. -G "Ninja"
cmake --build .

mkdir build\win64 && cd build\win64
cmake ..\.. -G "Ninja"
cmake --build .
```

## 安装

DS 驱动需要安装到 TWAIN 系统目录：

- **32 位**: `C:\Windows\twain_32\bntech\`
- **64 位**: `C:\Windows\twain_64\bntech\`

从构建输出目录复制以下文件：
1. `bntech_virtual_scanner.ds`
2. `FreeImage.dll`
3. `TWAIN_logo.png`

或使用 CMake install（需管理员权限）：
```batch
cmake --install build\win32 --prefix /
```

## 图片源设置

将测试图片放入：`%APPDATA%\bntech\images\`

支持的格式：PNG、JPG、JPEG、BMP、TIF、TIFF。文件按字母序排列，
每次 acquire 操作自动轮转到下一张。

如需重置扫描顺序，删除 `%APPDATA%\bntech\images\info.json`。

## 代码风格

项目遵循 [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)：
- `//` 行注释，句首大写，句末带句号
- 每条语句独占一行，禁止多语句压缩
- 正确的缩进、空格和大括号位置
- 变量使用 `snake_case`，类/函数使用 `CamelCase`
- 禁用 `using namespace std`，优先使用 `static_cast` 替代 C 风格转换

</details>

A minimal TWAIN 2.5 compatible virtual flatbed scanner Data Source DLL
for testing and development.  Designed to be recognized by standard TWAIN
applications (Twack32, Windows Fax and Scan) with the fewest possible
capabilities.

## Features

- **Flatbed scanner emulation**: Presents as a basic flatbed scanner with
  no ADF/duplex support.
- **Image source**: Reads images from `%APPDATA%\bntech\images`, sorted
  alphabetically.  Each acquire advances to the next image; wraps around
  when all are exhausted.
- **Default image**: Falls back to `TWAIN_logo.png` from the DS install
  directory when the images directory is empty.
- **Native transfer only**: Provides DIB-format image data via
  DAT_IMAGENATIVEXFER (the minimal transfer mechanism accepted by
  most applications).
- **No UI**: `ShowUI=TRUE` is accepted but no settings dialog is shown;
  the DS always scans with the currently negotiated capabilities.
- **Dual architecture**: Builds both 32-bit and 64-bit DS DLLs.
- **FreeImage**: Uses the FreeImage library for image loading and pixel
  type conversion (BW threshold, grayscale, color with R/B swap).
- **Index persistence**: Stores the next-image index in `info.json`
  under the images directory so that scan progress survives DLL unloads.

## Supported Capabilities

Only 8 capabilities are registered — the minimum required for flatbed
compatibility:

| Capability | Type | Container | Ops | Default | Values |
|---|---|---|---|---|---|
| `CAP_SUPPORTEDCAPS` | UINT16 | ARRAY | GET | — | The other 7 caps |
| `ICAP_XFERMECH` | UINT16 | ONEVALUE | All | TWSX_NATIVE | TWSX_NATIVE |
| `ICAP_PIXELTYPE` | UINT16 | ENUMERATION | All | TWPT_RGB | BW, GRAY, RGB |
| `ICAP_XRESOLUTION` | FIX32 | ENUMERATION | All | 300 | 150, 200, 300, 600 |
| `ICAP_YRESOLUTION` | FIX32 | ENUMERATION | All | 300 | 150, 200, 300, 600 |
| `CAP_FEEDERENABLED` | BOOL | ONEVALUE | GET | FALSE | FALSE |
| `ICAP_PIXELFLAVOR` | UINT16 | ONEVALUE | All | TWPF_CHOCOLATE | TWPF_CHOCOLATE |
| `CAP_UICONTROLLABLE` | BOOL | ONEVALUE | GET | TRUE | TRUE |

"All" operations = GET + GETCURRENT + GETDEFAULT + SET + RESET.

## Architecture

```
src/
├── capability.h / .cpp      Capability negotiation (8 caps)
├── twain_data_source.h/.cpp  DS state machine, message routing,
│                             native transfer, DIB construction,
│                             inlined DSM interface
├── ds_entry.cpp              DLL entry point (DS_Entry + DllMain)
├── virtual_scanner.h / .cpp  Image loading, pixel conversion,
│                             strip-based scan output
├── unit_convert.h / .cpp     TWAIN unit system and FIX32 utilities
├── platform.h                Windows platform macros (/FI included)
├── resource.h                Resource IDs
├── modules.md                Module descriptions
└── core.md                   Coding style rules
```

The DSM interface (TWAIN_32.dll loading, event signalling, memory
management) is inlined directly into `twain_data_source.cpp` as
file-scope static functions — no separate module.

## Building

### Prerequisites
- CMake 3.15+
- Visual Studio 2022 with C++ desktop development workload
- FreeImage library (included in `pub/external`)

### Quick Build
```batch
build.bat win32
build.bat win64
```

### Manual Build
```batch
mkdir build\win32 && cd build\win32
cmake ..\.. -G "Ninja"
cmake --build .

mkdir build\win64 && cd build\win64
cmake ..\.. -G "Ninja"
cmake --build .
```

## Installation

The DS must be installed to the TWAIN system directory:

- **32-bit**: `C:\Windows\twain_32\bntech\`
- **64-bit**: `C:\Windows\twain_64\bntech\`

Copy the following files from the build output:
1. `bntech_virtual_scanner.ds`
2. `FreeImage.dll`
3. `TWAIN_logo.png`

Or use CMake install (requires admin privileges):
```batch
cmake --install build\win32 --prefix /
```

## Image Source Setup

Place test images in: `%APPDATA%\bntech\images\`

Supported formats: PNG, JPG, JPEG, BMP, TIF, TIFF.  Files are sorted
alphabetically and cycled through on each acquire operation.

To reset the scan order, delete `%APPDATA%\bntech\images\info.json`.

## Code Style

Project follows [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html):
- `//` line comments with sentence capitalization and periods.
- One statement per line; no multi-statement compression.
- Proper indentation, spacing, and brace placement.
- `snake_case` for variables, `CamelCase` for classes/functions.
- No `using namespace std`.  Prefer `static_cast` over C-style casts.
