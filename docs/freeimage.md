# FreeImage 库介绍 / FreeImage Library Overview

本文档介绍 BN Tech Virtual Scanner 选用的图像处理库 FreeImage：功能、特性、与同类库的对比、许可分析、商用要求，以及常用代码示例（加载、放大、保存、BMP → PNG 转换）。

<details open>
<summary>中文说明</summary>

## 1. FreeImage 简介

[FreeImage](https://freeimage.sourceforge.io/) 是一个开源、跨平台的 C/C++ 图像加载、保存与基本图像处理库。它最早由 Hervé Drolon 发起，专注于"开发者友好的多格式图像 I/O"：用一致的 API 屏蔽 PNG、JPEG、TIFF、BMP、GIF、JPEG2000、RAW、HDR 等 30+ 种格式的差异。

最新稳定版本为 3.18.0（2018 年发布；社区一直在打补丁维护）。本项目使用的 FreeImage 二进制位于 `pub/external/freeimage/` 目录，附带 32 位 / 64 位 DLL、import lib 与头文件。

主要应用场景：

- 桌面应用的图像加载 / 缩略图生成 / 格式转换。
- TWAIN / 扫描类项目的图像 I/O（本项目即典型案例）。
- 游戏和 3D 渲染引擎的纹理预处理工具链。
- 科学影像、HDR 处理的轻量级前端。

## 2. 主要功能

### 2.1 文件格式支持

读取 / 写入（部分格式仅读）：

- 位图类：BMP、PNG、JPEG、JPEG 2000、GIF、TIFF、TARGA、PCX、PSD、ICO、Webp 等。
- 矢量 / 元文件：WBMP、XBM、XPM、PFM。
- HDR / 浮点：HDR（Radiance）、EXR（OpenEXR）、TIFF Float。
- 相机 RAW：CR2、NEF、ARW、DNG 等（基于 LibRaw / dcraw 集成）。
- 其他：JBIG、KOALA、IFF、SGI、PICT、PNM 等。

共约 30 余种格式，覆盖几乎所有常见与历史格式。

### 2.2 像素 / 位深

支持位深：

- 1-bit（黑白 / 调色板）
- 4-bit、8-bit（调色板 / 灰度）
- 16-bit（555 / 565、灰度、float）
- 24-bit / 32-bit（BGR / BGRA）
- 48-bit / 64-bit（每通道 16-bit RGB / RGBA）
- 浮点（每通道 32-bit / 96-bit / 128-bit）

支持的逻辑像素类型 (`FREE_IMAGE_TYPE`)：`FIT_BITMAP`、`FIT_UINT16`、`FIT_INT16`、`FIT_UINT32`、`FIT_INT32`、`FIT_FLOAT`、`FIT_DOUBLE`、`FIT_COMPLEX`、`FIT_RGB16`、`FIT_RGBA16`、`FIT_RGBF`、`FIT_RGBAF`。

### 2.3 图像处理

不是大而全的图像处理库，但内置一组实用功能：

- 几何变换：`FreeImage_Rescale`（多种滤波器）、`FreeImage_Rotate`、`FreeImage_FlipHorizontal/Vertical`、`FreeImage_Copy`、`FreeImage_Paste`。
- 色彩转换：`FreeImage_ConvertTo24Bits`、`FreeImage_ConvertTo32Bits`、`FreeImage_ConvertToGreyscale`、`FreeImage_ConvertToRGBF`。
- 二值化与抖动：`FreeImage_Threshold`、`FreeImage_Dither`（多种算法）。
- 颜色调整：`FreeImage_AdjustGamma`、`FreeImage_AdjustBrightness`、`FreeImage_AdjustContrast`、`FreeImage_Invert`。
- 通道操作：`FreeImage_GetChannel`、`FreeImage_SetChannel`。
- 调色板：`FreeImage_GetPalette`、`FreeImage_SetPalette`。
- DPI 元数据：`FreeImage_GetDotsPerMeterX/Y`、`FreeImage_SetDotsPerMeterX/Y`。
- 元数据（EXIF / IPTC / XMP / GeoTIFF）读写。
- 多页：TIFF / GIF / ICO 多页 / 多帧读写。
- 内存 I/O：从 `FIMEMORY` 流式读写而非走文件系统。

### 2.4 滤波器（重采样）

`FreeImage_Rescale` 支持滤波器：

| 滤波器 | 特点 |
|---|---|
| `FILTER_BOX`       | 最简单的盒式平均；速度最快、质量最差 |
| `FILTER_BILINEAR`  | 双线性；平滑但偏糊 |
| `FILTER_BICUBIC`   | 双三次；通用、稍锐 |
| `FILTER_BSPLINE`   | B 样条；平滑、低振铃 |
| `FILTER_CATMULLROM`| Catmull-Rom；锐度较高 |
| `FILTER_LANCZOS3`  | Lanczos3；最高质量、可能轻微振铃 |

本项目按 DPI 重采样时统一选 `FILTER_LANCZOS3`，参见 `docs/implement_dpi_design.md` §4.3。

### 2.5 多线程

FreeImage 自身不提供线程池；但是 API 是可重入的，前提是不同线程操作不同的 `FIBITMAP`。同一个 `FIBITMAP` 在多线程读写需要调用方加锁。

## 3. 特性

- **轻量**：核心 DLL ~3 MB（含主要解码器）；可裁剪。
- **跨平台**：Windows、Linux、macOS、FreeBSD、Solaris。
- **C 风格 API**：句柄 `FIBITMAP*` + 大量 `FreeImage_*` 函数；上手快、绑定到其他语言简单。
- **同时提供 C++ 封装** (`FreeImagePlus`)：RAII / 模板化封装，但项目少用。
- **格式插件架构**：内部按 FIF (`FREE_IMAGE_FORMAT`) 注册插件，可手动添加自定义格式。
- **统一调色板模型**：所有索引图都按 RGBQUAD 调色板看待，避免格式特异性代码。
- **支持完整元数据链路**：EXIF / IPTC / XMP / Animation / GeoTIFF；扫描场景常用的 DPI 字段自动读写。
- **从 `FIMEMORY` 工作**：把图像加载 / 保存到内存缓冲区，方便集成网络 / 流式场景。

## 4. 与同类库的对比

### 4.1 对比对象

| 库 | 类型 | 主要用途 |
|---|---|---|
| FreeImage          | C 库 + DLL          | 多格式 I/O + 轻量处理 |
| libpng / libjpeg / libtiff | C 库（按格式） | 单一格式专精 |
| stb_image          | 单头文件 C 库       | 极简图像加载 |
| OpenCV             | 大型 C++ 库         | 计算机视觉 + 图像处理 |
| ImageMagick / GraphicsMagick | C/C++ + CLI | 全功能图像处理 |
| WIC（Windows Imaging Component） | COM      | Windows 原生图像 I/O |
| Pillow (PIL)       | Python 库           | Python 生态图像处理 |
| Skia               | C++ 渲染库          | 2D 渲染 / Chrome 用 |
| DevIL              | C 库                | 多格式 I/O |

### 4.2 特性对比

| 维度 | FreeImage | OpenCV | stb_image | libpng+jpeg+tiff | ImageMagick | WIC |
|---|---|---|---|---|---|---|
| 支持格式数量 | 30+ | 几十种（依赖 libjpeg 等） | 6 种（PNG/JPG/BMP/TGA/PSD/GIF, 仅加载） | 各自 1 种 | 100+ | 10+（含 RAW 插件） |
| 多页 (TIFF / GIF) | ✅ | 部分 | ❌ | TIFF ✅ | ✅ | ✅ |
| HDR / 浮点 | ✅ | ✅ | 部分 | TIFF 浮点 | ✅ | 部分 |
| 元数据 (EXIF/XMP/IPTC) | ✅ | 有限 | ❌ | ❌ | ✅ | ✅ |
| 跨平台 | ✅ | ✅ | ✅ | ✅ | ✅ | Windows-only |
| API 复杂度 | 中（C 风格） | 高（C++/Python） | 极低 | 中（按格式各异） | 高（多 API + CLI） | 高（COM） |
| 二进制体积 | ~3 MB | 数十 MB | 0（头文件） | 各 ~1 MB | ~30 MB | 系统自带 |
| 商业友好度 | 复杂（见 §5） | ✅ Apache 2.0 | ✅ Public Domain / MIT | ✅ 各自宽松 | ✅ ImageMagick License | ✅ 系统组件 |
| 处理功能 | 基本 | 强（CV 全套） | ❌（只加载） | ❌ | 强 | 基本 |
| 维护活跃度 | 低（2018 后只补丁） | 高 | 中 | 高 | 高 | 高（微软） |

### 4.3 选择策略（针对扫描类项目）

- 只需要"加载几种主流格式"的极简场景：用 `stb_image` 更简单。
- Windows-only 且能接受 COM 复杂度：WIC 性能极佳、无第三方依赖。
- 需要 CV / 阈值 / 边缘检测：OpenCV 一站式但体积大。
- 需要广格式 + 基本处理 + 跨平台 + DLL 形式且代码体积小：FreeImage 仍是性价比最佳选择 —— 这也是本项目选它的原因。
- 需要 100% 商用安全：libpng + libjpeg-turbo + libtiff 组合更可控（见 §5）。

### 4.4 性能简评

- 单图加载 / 保存性能：与 libpng / libjpeg 同量级（FreeImage 内部就是封装它们）。
- 重采样：`LANCZOS3` 在大图上明显慢于 OpenCV 的 SIMD 实现；扫描类小批量场景无压力。
- 多线程：FreeImage 不并行；OpenCV 内部 TBB / OpenMP 加速。

## 5. 许可分析

### 5.1 双重许可

FreeImage 采用 **双重许可（dual license）**：

- **FreeImage Public License (FIPL)**，基于 Mozilla Public License 1.1 (MPL 1.1)。
- **GNU General Public License (GPL) v2 或 v3**。

使用者可以选择其中之一。

### 5.2 FIPL（基于 MPL 1.1）要点

MPL 1.1 是 file-level copyleft（文件级 copyleft），约束如下：

- 修改 FreeImage 源文件后必须公开修改内容（即对 FreeImage 自身代码的变动需开源）。
- 链接 FreeImage 的应用本身可以保持私有 / 闭源（这点与 LGPL 类似）。
- 必须在分发文档或软件中包含 FIPL 许可声明，告知用户使用了 FreeImage。
- 不能用作者 / 贡献者名义为产品背书。

通俗结论：**只用 FreeImage（不改它的源码），你的应用可以闭源商用**；但你必须随产品分发许可声明。

### 5.3 GPL 路径要点

如果选择 GPL，则整个使用 FreeImage 的应用必须以 GPL 兼容许可证发布并提供源码。商用闭源软件几乎不会选这条路径。

### 5.4 内部依赖的许可

FreeImage 内部静态链接了若干第三方解码库，这是 FreeImage 商用风险的真正所在：

| 第三方组件 | 许可 | 风险等级 |
|---|---|---|
| libpng        | libpng License (BSD-style) | 低 |
| libjpeg / libjpeg-turbo | IJG License / BSD       | 低 |
| libtiff       | BSD-like                   | 低 |
| zlib          | zlib License               | 低 |
| OpenJPEG      | BSD 2-clause               | 低 |
| LibRaw        | LGPL 2.1 / CDDL / Commercial | **中**（LGPL 触发动态链接义务） |
| OpenEXR       | BSD 3-clause               | 低 |
| LibWebP       | BSD-style                  | 低 |
| jxrlib        | BSD-style                  | 低 |

最大的注意点是 **LibRaw**（RAW 相机文件解码器）：以 LGPL 2.1 形式静态链接到 FreeImage 内可能让你的应用承担 LGPL 义务（提供 object 文件以便用户重链接，或动态链接）。如果你不需要 RAW 支持，可以从源码构建 FreeImage 时关掉 LibRaw 模块。

### 5.5 商用要求

实际商用项目使用 FreeImage 应满足的事项：

1. **保留版权与许可文本**：在 About 对话框、文档或 LICENSE 文件里附 FIPL 全文与 FreeImage 版权声明。例如：
   ```
   This software uses the FreeImage open source image library.
   See http://freeimage.sourceforge.net for details.
   FreeImage is used under the FreeImage Public License (FIPL), version 1.0.
   ```
2. **不修改 FreeImage 源码**：如果使用预编译 DLL，则不会触发"修改公开"义务。
3. **如修改源码**：把修改后的源码公开（典型做法是在公司公开仓库挂一份 patch）。
4. **决定是否包含 LibRaw**：商业项目如不需要 RAW 解码，建议从源码构建 FreeImage 并禁用 LibRaw 插件，规避 LGPL 链接问题。
5. **DLL 分发**：把 `FreeImage.dll`（或自定义构建的 .dll）与应用一起分发；不要把 FreeImage 静态编入你的核心闭源模块以避免边界模糊。
6. **更新追踪**：FreeImage 自 2018 年起更新缓慢；如果合规非常严格，建议自己维护一个 fork 并跟踪 CVE。

### 5.6 关于"声称商用免费"的常见误区

- "FreeImage 是 free 的所以能商用" — 部分正确：FIPL 允许商用，但要满足声明义务。
- "我可以静态链接到我的 EXE" — 技术上可以，但因为静态链接会"模糊文件边界"，FIPL 的 file-level copyleft 解释存在争议；本项目按"DLL 动态链接 + 声明"的最保守做法走。
- "GPL 路径更安全" — 对闭源商用反而不安全；只在你愿意整个产品 GPL 时才选。

## 6. 在本项目中的使用方式

本仓库的 `pub/external/freeimage/` 已包含官方预编译 DLL，`CMakeLists.txt` 通过 `find_library / target_link_libraries` 链接。运行时把 `FreeImage.dll` 放在 `.ds` 同目录（详见 `README.md` 的 "Installed files"）。

关键调用点：

- 加载源图：`FreeImage_GetFileTypeU` + `FreeImage_LoadU`。
- 颜色 / 位深：`FreeImage_ConvertTo24Bits` / `FreeImage_ConvertToGreyscale` / `FreeImage_Threshold`。
- 尺寸：`FreeImage_Rescale(..., FILTER_LANCZOS3)`。
- DPI 元数据：`FreeImage_SetDotsPerMeterX / Y`。
- 保存输出：`FreeImage_Save(FIF_PNG | FIF_JPEG | FIF_BMP | FIF_TIFF, ...)`。
- DIB 输出（Native Transfer）：`FreeImage_GetWidth / GetHeight / GetBPP / GetScanLine / GetPalette` 直接拼 `BITMAPINFOHEADER`。

## 7. 代码示例

> 以下示例为最小可运行 demo；省略错误处理细节，实际产品代码请补充资源释放与失败分支。

### 7.1 初始化与清理

```cpp
#include <FreeImage.h>

int main() {
  // 静态库需手动初始化；DLL 版本可省略（启动时自动 Init）。
  // FreeImage_Initialise();

  // ... 业务代码 ...

  // FreeImage_DeInitialise();
  return 0;
}
```

### 7.2 加载图片

```cpp
FIBITMAP* LoadImage(const wchar_t* path) {
  FREE_IMAGE_FORMAT fif = FreeImage_GetFileTypeU(path, 0);
  if (fif == FIF_UNKNOWN) {
    // 退化到扩展名嗅探。
    fif = FreeImage_GetFIFFromFilenameU(path);
  }
  if (fif == FIF_UNKNOWN || !FreeImage_FIFSupportsReading(fif)) {
    return nullptr;
  }
  FIBITMAP* bmp = FreeImage_LoadU(fif, path, 0);
  return bmp;  // 调用方负责 FreeImage_Unload(bmp)
}
```

### 7.3 图片放大两倍（高质量 Lanczos3）

```cpp
FIBITMAP* RescaleTwoTimes(FIBITMAP* src) {
  if (!src) return nullptr;
  int src_w = FreeImage_GetWidth(src);
  int src_h = FreeImage_GetHeight(src);
  int dst_w = src_w * 2;
  int dst_h = src_h * 2;
  FIBITMAP* dst = FreeImage_Rescale(src, dst_w, dst_h, FILTER_LANCZOS3);
  return dst;  // 调用方负责 Unload；原 src 也要单独 Unload
}
```

### 7.4 保存图片

```cpp
bool SaveImage(FIBITMAP* bmp, const wchar_t* path, FREE_IMAGE_FORMAT fif) {
  if (!bmp) return false;
  int flags = 0;
  if (fif == FIF_JPEG) flags = JPEG_QUALITYGOOD;            // ≈85
  if (fif == FIF_TIFF) flags = TIFF_LZW;                    // 8/24bit 用 LZW
  if (fif == FIF_PNG)  flags = PNG_DEFAULT;                 // 默认 zlib
  return FreeImage_SaveU(fif, bmp, path, flags) == TRUE;
}
```

### 7.5 完整示例：BMP → PNG 转换 + 放大两倍 + 写 DPI

```cpp
#include <FreeImage.h>
#include <cstdio>

int wmain(int argc, wchar_t** argv) {
  if (argc < 3) {
    std::wprintf(L"usage: bmp2png <in.bmp> <out.png>\n");
    return 1;
  }
  const wchar_t* in_path  = argv[1];
  const wchar_t* out_path = argv[2];

  // 1. 加载 BMP（FreeImage 按签名识别，扩展名只是辅助）。
  FREE_IMAGE_FORMAT in_fif = FreeImage_GetFileTypeU(in_path, 0);
  if (in_fif == FIF_UNKNOWN) {
    in_fif = FreeImage_GetFIFFromFilenameU(in_path);
  }
  FIBITMAP* src = FreeImage_LoadU(in_fif, in_path, 0);
  if (!src) {
    std::wprintf(L"failed to load: %ls\n", in_path);
    return 2;
  }

  // 2. 统一到 24-bit BGR，避免索引 / palette 在重采样后出现意外。
  FIBITMAP* rgb24 = FreeImage_ConvertTo24Bits(src);
  FreeImage_Unload(src);
  if (!rgb24) {
    std::wprintf(L"convert to 24-bit failed\n");
    return 3;
  }

  // 3. 放大两倍（Lanczos3）。
  int src_w = FreeImage_GetWidth(rgb24);
  int src_h = FreeImage_GetHeight(rgb24);
  FIBITMAP* scaled = FreeImage_Rescale(rgb24, src_w * 2, src_h * 2,
                                       FILTER_LANCZOS3);
  FreeImage_Unload(rgb24);
  if (!scaled) {
    std::wprintf(L"rescale failed\n");
    return 4;
  }

  // 4. 写 DPI 元数据：300 DPI ≈ 300 * 39.37 pixels-per-meter。
  unsigned ppm = static_cast<unsigned>(300.0 * 39.37);
  FreeImage_SetDotsPerMeterX(scaled, ppm);
  FreeImage_SetDotsPerMeterY(scaled, ppm);

  // 5. 保存为 PNG。
  BOOL ok = FreeImage_SaveU(FIF_PNG, scaled, out_path, PNG_DEFAULT);
  FreeImage_Unload(scaled);

  if (!ok) {
    std::wprintf(L"failed to save: %ls\n", out_path);
    return 5;
  }
  std::wprintf(L"ok: %ls -> %ls (2x, 300dpi)\n", in_path, out_path);
  return 0;
}
```

编译（Windows + MSVC，假设 FreeImage 头文件与 import lib 已在 include / lib 路径）：

```bat
cl /EHsc /W4 bmp2png.cpp /link FreeImage.lib
bmp2png in.bmp out.png
```

运行后 `out.png` 是输入 BMP 的 2 倍尺寸、300 DPI 的 PNG 文件。

### 7.6 内存 I/O 版本（不落盘）

```cpp
FIMEMORY* SaveToMemory(FIBITMAP* bmp, FREE_IMAGE_FORMAT fif) {
  FIMEMORY* mem = FreeImage_OpenMemory();
  FreeImage_SaveToMemory(fif, bmp, mem, 0);
  return mem;  // 调用方负责 FreeImage_CloseMemory(mem)
}

void ReadFromMemory(BYTE* buffer, DWORD size) {
  FIMEMORY* mem = FreeImage_OpenMemory(buffer, size);
  FREE_IMAGE_FORMAT fif = FreeImage_GetFileTypeFromMemory(mem, 0);
  FIBITMAP* bmp = FreeImage_LoadFromMemory(fif, mem, 0);
  // ...
  FreeImage_Unload(bmp);
  FreeImage_CloseMemory(mem);
}
```

适合 HTTP 上传 / 数据库 BLOB 场景。

## 8. 小结

- 优点：API 简洁、格式多、二进制小、跨平台、扫描类项目几乎"开箱即用"。
- 缺点：维护节奏慢、滤波器非 SIMD、商用许可需要小心 LibRaw 等内部依赖。
- 在 BN Tech Virtual Scanner 中，FreeImage 是 `VirtualScanner` 的底层支柱：从加载到 DPI 缩放、像素类型转换、最终 DIB / 文件输出全靠它，是 TWAIN 协议层之外几乎所有像素工作的唯一执行者。

</details>

<details>
<summary>English</summary>

## 1. What is FreeImage

[FreeImage](https://freeimage.sourceforge.io/) is an open-source, cross-platform C/C++ library for loading, saving, and lightly processing images. Originally started by Hervé Drolon, it focuses on developer-friendly multi-format image I/O, providing a uniform API across 30+ formats including PNG, JPEG, TIFF, BMP, GIF, JPEG 2000, RAW, and HDR.

The latest stable release is 3.18.0 (2018; the community still publishes patches). This project bundles the official binaries under `pub/external/freeimage/` with 32-bit / 64-bit DLLs, import libs, and headers.

Typical use cases:

- Desktop image loading / thumbnail / format conversion tools.
- TWAIN / scanner projects (this is exactly what this project does).
- Texture preprocessing pipelines for games / 3D engines.
- Lightweight front-ends for scientific / HDR imaging.

## 2. Features

### 2.1 Formats

Reads and writes (some are read-only):

- Raster: BMP, PNG, JPEG, JPEG 2000, GIF, TIFF, TARGA, PCX, PSD, ICO, WebP, etc.
- Vector / metafile: WBMP, XBM, XPM, PFM.
- HDR / float: HDR (Radiance), EXR (OpenEXR), TIFF Float.
- Camera RAW: CR2, NEF, ARW, DNG, etc. (via LibRaw / dcraw).
- Misc: JBIG, KOALA, IFF, SGI, PICT, PNM, ...

About 30+ formats total.

### 2.2 Pixel / bit depth

- 1, 4, 8 bpp (palette / grayscale).
- 16 bpp (555 / 565 / gray / float).
- 24, 32 bpp (BGR / BGRA).
- 48, 64 bpp (16-bit RGB / RGBA).
- Floating point (32 / 96 / 128 bpp).

Logical types (`FREE_IMAGE_TYPE`): `FIT_BITMAP`, `FIT_UINT16`, `FIT_INT16`, `FIT_UINT32`, `FIT_INT32`, `FIT_FLOAT`, `FIT_DOUBLE`, `FIT_COMPLEX`, `FIT_RGB16`, `FIT_RGBA16`, `FIT_RGBF`, `FIT_RGBAF`.

### 2.3 Image processing

Not a full image-processing library, but it ships useful primitives:

- Geometry: `FreeImage_Rescale` (multiple filters), `FreeImage_Rotate`, `FreeImage_FlipHorizontal/Vertical`, `FreeImage_Copy`, `FreeImage_Paste`.
- Color: `FreeImage_ConvertTo24Bits`, `FreeImage_ConvertTo32Bits`, `FreeImage_ConvertToGreyscale`, `FreeImage_ConvertToRGBF`.
- Thresholding / dithering: `FreeImage_Threshold`, `FreeImage_Dither` (Floyd-Steinberg, Bayer, ...).
- Color adjustment: `AdjustGamma / Brightness / Contrast`, `Invert`.
- Channels: `GetChannel / SetChannel`.
- Palette: `GetPalette / SetPalette`.
- DPI metadata: `GetDotsPerMeterX/Y`, `SetDotsPerMeterX/Y`.
- EXIF / IPTC / XMP / GeoTIFF metadata.
- Multi-page TIFF / GIF / ICO.
- Memory I/O via `FIMEMORY` (no filesystem required).

### 2.4 Resampling filters

| Filter | Notes |
|---|---|
| `FILTER_BOX`        | Fastest, lowest quality |
| `FILTER_BILINEAR`   | Smooth but blurry |
| `FILTER_BICUBIC`    | General-purpose, slightly sharper |
| `FILTER_BSPLINE`    | Smooth, low ringing |
| `FILTER_CATMULLROM` | Sharp |
| `FILTER_LANCZOS3`   | Highest quality, possible ringing |

This project uses `FILTER_LANCZOS3` for DPI rescaling (see `docs/implement_dpi_design.md` §4.3).

### 2.5 Threading

FreeImage has no built-in threading. The API is re-entrant as long as different threads operate on different `FIBITMAP` instances; concurrent access to the same `FIBITMAP` must be synchronized by the caller.

## 3. Characteristics

- **Lightweight**: ~3 MB DLL with all main decoders; trimmable.
- **Cross-platform**: Windows, Linux, macOS, FreeBSD, Solaris.
- **C-style API**: `FIBITMAP*` handle + `FreeImage_*` functions; easy bindings.
- **C++ wrapper** (`FreeImagePlus`) is available but rarely used.
- **Plugin architecture**: formats registered as `FREE_IMAGE_FORMAT` plugins, custom formats can be added.
- **Uniform palette model**: indexed images all expose RGBQUAD palettes.
- **Full metadata** (EXIF / IPTC / XMP / Animation / GeoTIFF).
- **Memory I/O** through `FIMEMORY` for streaming / network use.

## 4. Comparison

### 4.1 Candidates

| Library | Form | Use |
|---|---|---|
| FreeImage | C library + DLL | Multi-format I/O + light processing |
| libpng / libjpeg / libtiff | per-format C libs | Single-format specialists |
| stb_image | header-only C | Minimal loading |
| OpenCV | large C++ lib | CV + image processing |
| ImageMagick / GraphicsMagick | C/C++ + CLI | Full processing suite |
| WIC | COM | Native Windows imaging |
| Pillow (PIL) | Python | Python ecosystem |
| Skia | C++ | 2D rendering (Chrome) |
| DevIL | C lib | Multi-format I/O |

### 4.2 Feature matrix

| Aspect | FreeImage | OpenCV | stb_image | libpng+jpeg+tiff | ImageMagick | WIC |
|---|---|---|---|---|---|---|
| Formats | 30+ | tens | 6 (load only) | 1 each | 100+ | 10+ |
| Multi-page (TIFF/GIF) | yes | partial | no | TIFF yes | yes | yes |
| HDR / float | yes | yes | partial | TIFF float | yes | partial |
| Metadata (EXIF/XMP/IPTC) | yes | limited | no | no | yes | yes |
| Cross-platform | yes | yes | yes | yes | yes | Windows only |
| API complexity | medium | high | very low | medium | high | high (COM) |
| Binary size | ~3 MB | tens of MB | 0 (header) | ~1 MB each | ~30 MB | system |
| Commercial-friendly | nuanced (§5) | yes (Apache 2.0) | yes (Public Domain / MIT) | yes (each permissive) | yes | yes (system) |
| Processing | basic | rich (CV) | none | none | rich | basic |
| Active maintenance | low (patches since 2018) | high | medium | high | high | high |

### 4.3 Choosing for a scanner project

- Only need a few major formats: `stb_image` is simplest.
- Windows-only, OK with COM: WIC is fastest and dependency-free.
- Need CV / thresholding / edge detection: OpenCV, but heavy.
- Need many formats + basic processing + cross-platform + small DLL: FreeImage is the best balance — that's why this project uses it.
- Need 100% safe commercial story: hand-pick libpng + libjpeg-turbo + libtiff (see §5).

### 4.4 Performance

- Per-image load/save: comparable to libpng / libjpeg (FreeImage simply wraps them).
- Resampling: `LANCZOS3` is noticeably slower than OpenCV's SIMD; fine for one-image-per-scan workloads.
- No internal threading; OpenCV uses TBB / OpenMP internally.

## 5. License analysis

### 5.1 Dual license

FreeImage is dual-licensed:

- **FreeImage Public License (FIPL)**, based on Mozilla Public License 1.1 (MPL 1.1).
- **GPL v2 or v3**.

Pick either.

### 5.2 FIPL highlights

MPL 1.1 is a file-level copyleft:

- Modifications to FreeImage source files must be published.
- Applications linking FreeImage may stay closed-source.
- Distribute the FIPL license notice with your product.
- No endorsement using contributors' names.

Bottom line: **using FreeImage without modifying its source allows closed-source commercial use**, provided you carry the notice.

### 5.3 GPL path

Choosing GPL forces your entire application to be GPL-compatible with full source disclosure. Almost no closed-source commercial product picks this path.

### 5.4 Internal dependencies

FreeImage statically embeds several decoder libraries; these are where most commercial-license risk lives:

| Component | License | Risk |
|---|---|---|
| libpng | libpng (BSD-like) | low |
| libjpeg / libjpeg-turbo | IJG / BSD | low |
| libtiff | BSD-like | low |
| zlib | zlib | low |
| OpenJPEG | BSD 2-clause | low |
| LibRaw | LGPL 2.1 / CDDL / commercial | **medium** (LGPL implies dynamic-linking duties) |
| OpenEXR | BSD 3-clause | low |
| LibWebP | BSD-like | low |
| jxrlib | BSD-like | low |

The notable concern is **LibRaw**: linking it statically may impose LGPL obligations (provide object files for relinking, or link dynamically). If RAW support is not required, build FreeImage from source with LibRaw disabled.

### 5.5 Commercial checklist

1. **Keep notices**: include the FIPL text and FreeImage copyright in About / docs / LICENSE.
   ```
   This software uses the FreeImage open source image library.
   See http://freeimage.sourceforge.net for details.
   FreeImage is used under the FreeImage Public License (FIPL), version 1.0.
   ```
2. **Do not modify FreeImage source** if you want to skip publishing changes.
3. **Publish patches** if you modify FreeImage.
4. **Decide on LibRaw**: disable it if you do not need RAW decoding.
5. **Ship as a DLL**: keep FreeImage as a clearly separated dynamic library to avoid boundary debates.
6. **Track updates**: FreeImage has slowed since 2018; maintain a private fork for CVE follow-ups if compliance is strict.

### 5.6 Common misconceptions

- "FreeImage is free so it's safe commercially" — true if you respect the notice and do not modify source.
- "I can static-link it into my EXE" — technically possible, but file-level copyleft interpretation is contested; this project uses dynamic linking + notice for safety.
- "GPL is safer" — no, GPL forces source disclosure of your whole product.

## 6. Usage in this project

The official prebuilt DLLs live in `pub/external/freeimage/`. `CMakeLists.txt` wires them via `find_library / target_link_libraries`. At runtime `FreeImage.dll` ships next to the `.ds` file (see README "Installed files").

Key call sites:

- Load source image: `FreeImage_GetFileTypeU` + `FreeImage_LoadU`.
- Color / depth: `FreeImage_ConvertTo24Bits` / `FreeImage_ConvertToGreyscale` / `FreeImage_Threshold`.
- Resize: `FreeImage_Rescale(..., FILTER_LANCZOS3)`.
- DPI metadata: `FreeImage_SetDotsPerMeterX / Y`.
- Save: `FreeImage_Save(FIF_PNG | FIF_JPEG | FIF_BMP | FIF_TIFF, ...)`.
- Native Transfer DIB: `FreeImage_GetWidth / GetHeight / GetBPP / GetScanLine / GetPalette` feed directly into `BITMAPINFOHEADER`.

## 7. Code samples

> Minimal runnable snippets; production code should add full error-handling and resource release.

### 7.1 Init / shutdown

```cpp
#include <FreeImage.h>

int main() {
  // Static build needs explicit init; DLL version auto-inits at load.
  // FreeImage_Initialise();

  // ... work ...

  // FreeImage_DeInitialise();
  return 0;
}
```

### 7.2 Load an image

```cpp
FIBITMAP* LoadImage(const wchar_t* path) {
  FREE_IMAGE_FORMAT fif = FreeImage_GetFileTypeU(path, 0);
  if (fif == FIF_UNKNOWN) fif = FreeImage_GetFIFFromFilenameU(path);
  if (fif == FIF_UNKNOWN || !FreeImage_FIFSupportsReading(fif)) return nullptr;
  return FreeImage_LoadU(fif, path, 0);  // caller frees via FreeImage_Unload
}
```

### 7.3 Upscale 2x (Lanczos3)

```cpp
FIBITMAP* RescaleTwoTimes(FIBITMAP* src) {
  if (!src) return nullptr;
  int w = FreeImage_GetWidth(src);
  int h = FreeImage_GetHeight(src);
  return FreeImage_Rescale(src, w * 2, h * 2, FILTER_LANCZOS3);
}
```

### 7.4 Save an image

```cpp
bool SaveImage(FIBITMAP* bmp, const wchar_t* path, FREE_IMAGE_FORMAT fif) {
  if (!bmp) return false;
  int flags = 0;
  if (fif == FIF_JPEG) flags = JPEG_QUALITYGOOD;  // ~85
  if (fif == FIF_TIFF) flags = TIFF_LZW;
  if (fif == FIF_PNG)  flags = PNG_DEFAULT;
  return FreeImage_SaveU(fif, bmp, path, flags) == TRUE;
}
```

### 7.5 Full example: BMP → PNG with 2x upscale and DPI metadata

```cpp
#include <FreeImage.h>
#include <cstdio>

int wmain(int argc, wchar_t** argv) {
  if (argc < 3) {
    std::wprintf(L"usage: bmp2png <in.bmp> <out.png>\n");
    return 1;
  }
  const wchar_t* in_path  = argv[1];
  const wchar_t* out_path = argv[2];

  // 1. Load BMP. FreeImage detects format by signature; extension is a hint.
  FREE_IMAGE_FORMAT in_fif = FreeImage_GetFileTypeU(in_path, 0);
  if (in_fif == FIF_UNKNOWN) in_fif = FreeImage_GetFIFFromFilenameU(in_path);
  FIBITMAP* src = FreeImage_LoadU(in_fif, in_path, 0);
  if (!src) { std::wprintf(L"failed to load: %ls\n", in_path); return 2; }

  // 2. Normalize to 24-bit BGR to avoid surprises after rescaling.
  FIBITMAP* rgb24 = FreeImage_ConvertTo24Bits(src);
  FreeImage_Unload(src);
  if (!rgb24) { std::wprintf(L"convert to 24-bit failed\n"); return 3; }

  // 3. Upscale 2x with Lanczos3.
  int src_w = FreeImage_GetWidth(rgb24);
  int src_h = FreeImage_GetHeight(rgb24);
  FIBITMAP* scaled = FreeImage_Rescale(rgb24, src_w * 2, src_h * 2,
                                       FILTER_LANCZOS3);
  FreeImage_Unload(rgb24);
  if (!scaled) { std::wprintf(L"rescale failed\n"); return 4; }

  // 4. Write DPI metadata: 300 DPI ~= 300 * 39.37 pixels per meter.
  unsigned ppm = static_cast<unsigned>(300.0 * 39.37);
  FreeImage_SetDotsPerMeterX(scaled, ppm);
  FreeImage_SetDotsPerMeterY(scaled, ppm);

  // 5. Save as PNG.
  BOOL ok = FreeImage_SaveU(FIF_PNG, scaled, out_path, PNG_DEFAULT);
  FreeImage_Unload(scaled);
  if (!ok) { std::wprintf(L"failed to save: %ls\n", out_path); return 5; }

  std::wprintf(L"ok: %ls -> %ls (2x, 300dpi)\n", in_path, out_path);
  return 0;
}
```

Build (MSVC):

```bat
cl /EHsc /W4 bmp2png.cpp /link FreeImage.lib
bmp2png in.bmp out.png
```

The resulting `out.png` is the input BMP at 2x size with 300 DPI metadata.

### 7.6 Memory I/O variant (no filesystem)

```cpp
FIMEMORY* SaveToMemory(FIBITMAP* bmp, FREE_IMAGE_FORMAT fif) {
  FIMEMORY* mem = FreeImage_OpenMemory();
  FreeImage_SaveToMemory(fif, bmp, mem, 0);
  return mem;  // caller frees with FreeImage_CloseMemory
}

void ReadFromMemory(BYTE* buffer, DWORD size) {
  FIMEMORY* mem = FreeImage_OpenMemory(buffer, size);
  FREE_IMAGE_FORMAT fif = FreeImage_GetFileTypeFromMemory(mem, 0);
  FIBITMAP* bmp = FreeImage_LoadFromMemory(fif, mem, 0);
  // ...
  FreeImage_Unload(bmp);
  FreeImage_CloseMemory(mem);
}
```

Useful for HTTP uploads / database BLOBs.

## 8. Summary

- Pros: clean API, many formats, small binary, cross-platform, near-zero ceremony for scanner-style projects.
- Cons: slow release cadence, no SIMD filters, commercial licensing requires care around LibRaw.
- In BN Tech Virtual Scanner, FreeImage is the foundation of `VirtualScanner`: loading, DPI rescaling, pixel-type conversion, and final DIB / file output all run through it. Outside the TWAIN protocol layer, virtually every pixel operation is delegated to FreeImage.

</details>
