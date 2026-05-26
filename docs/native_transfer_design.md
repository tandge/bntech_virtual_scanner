# Native Transfer Mode Design

Design notes for the TWAIN Native Transfer (`TWSX_NATIVE`) path in BN Tech Virtual Scanner.

<details open>
<summary>中文说明</summary>

## 1. 需求

虚拟扫描仪必须支持 TWAIN 的默认 Native Transfer 模式 (`ICAP_XFERMECH = TWSX_NATIVE`)：DS 把整张图像组装成一个 DIB (Device Independent Bitmap) 内存块，通过 DSM 分配的句柄一次性返回给应用。

主要需求：

- `ICAP_XFERMECH` 默认值为 `TWSX_NATIVE`，应用即使不做任何能力协商也能跑 Native Transfer 流程。
- 支持 `DG_IMAGE / DAT_IMAGENATIVEXFER / MSG_GET`，返回一个可被应用 `GlobalLock` / `DSM_MemLock` 的 `TW_HANDLE`。
- DIB 内容必须包含 `BITMAPINFOHEADER` + 调色板（如适用）+ 像素数据，按 DIB 自底向上的行序排列，每行按 4 字节对齐。
- 支持 1-bit (BW)、8-bit (灰度) 和 24-bit (RGB) 三种像素格式：
  - 1-bit 带 2 项调色板（黑、白）。
  - 8-bit 带 256 项灰阶调色板。
  - 24-bit 不带调色板，像素顺序为 BGR（Windows DIB 约定）。
- `TW_IMAGEINFO` 字段必须与 DIB 内容完全一致：`ImageWidth / ImageLength`、`BitsPerPixel`、`SamplesPerPixel`、`BitsPerSample`、`XResolution / YResolution`、`PixelType`、`Compression = TWCP_NONE`。
- 内存必须使用 DSM 提供的 `DSM_MemAllocate / DSM_MemLock / DSM_MemUnlock / DSM_MemFree`；DSM 没有提供时才回退到 `GlobalAlloc / GlobalLock` 等。
- DIB 中的 `biXPelsPerMeter` / `biYPelsPerMeter` 与 `TW_IMAGEINFO.XResolution / YResolution` 必须同步，便于应用保存出来的文件保留正确 DPI。
- 通过 `ICAP_UNITS = TWUN_INCHES` 声明分辨率单位为 DPI/PPI。
- Native Transfer 必须遵循 TWAIN 状态机：`kEnabled (5) -> kXferReady (6) -> kXferring (7) -> kEnabled (5)`，转换由 `MSG_XFERREADY` / `DAT_IMAGENATIVEXFER / MSG_GET` / `DAT_PENDINGXFERS / MSG_ENDXFER` 驱动。

非功能性需求：

- 设备模拟：本项目作为虚拟平板扫描仪，`pending_xfers_.Count` 固定为 1（无 ADF）。
- 大图（>64 KB）必须能正常工作，不能让任何中间缓冲一次性 alloc 失败。
- 与 File Transfer 共用同一份图像准备代码 (`acquireImage` + `preScanPrep`)，输出阶段才分叉。

## 2. 领域知识

### 2.1 TWAIN Native Transfer 协议序列

```text
1. App: DG_CONTROL / DAT_USERINTERFACE / MSG_ENABLEDS
   DS:  acquireImage(), MSG_XFERREADY (state 5 -> 6)
2. App: DG_CONTROL / DAT_EVENT / MSG_PROCESSEVENT
3. App: DG_IMAGE   / DAT_IMAGEINFO / MSG_GET
4. App: DG_IMAGE   / DAT_IMAGENATIVEXFER / MSG_GET
   DS:  transfer() + getDibImage(), 返回 TW_HANDLE, state 6 -> 7
5. App: DG_CONTROL / DAT_PENDINGXFERS / MSG_ENDXFER  // state 7 -> 5
6. App: DG_CONTROL / DAT_USERINTERFACE / MSG_DISABLEDS // state 5 -> 4
```

返回码：

- 成功取得 DIB：`TWRC_XFERDONE`。
- 应用取消：`TWRC_CANCEL`。
- 状态错乱：`TWRC_FAILURE + TWCC_SEQERROR`。
- 分配失败：`TWRC_FAILURE + TWCC_LOWMEMORY`。

### 2.2 Windows DIB 内存布局

```text
+---------------------------+
| BITMAPINFOHEADER (40B)    |
+---------------------------+
| Color palette (optional)  |
|   1-bit:  2 * RGBQUAD     |
|   8-bit:  256 * RGBQUAD   |
|   24-bit: none            |
+---------------------------+
| Pixel data                |
|   Bottom-up rows          |
|   Each row padded to 4B   |
+---------------------------+
```

约束：

- 行字节数 = `((width * bpp) + 31) / 32 * 4`（4 字节对齐）。
- 行序为自底向上（第 0 行是图像最下面一行）。
- 24-bit 像素顺序为 BGR，不是 RGB。
- 调色板 `RGBQUAD` 字段顺序为 `{B, G, R, reserved}`。

### 2.3 DSM 内存接口

TWAIN 2.4+ 通过 `DAT_ENTRYPOINT / MSG_SET` 把内存函数指针传给 DS：

```c
TW_HANDLE  DSM_MemAllocate(TW_UINT32 size);
void       DSM_MemFree(TW_HANDLE handle);
TW_MEMREF  DSM_MemLock(TW_HANDLE handle);
void       DSM_MemUnlock(TW_HANDLE handle);
```

必须用 DSM 函数分配返回给应用的内存，否则跨进程或跨 DSM 版本释放可能失败。当 DSM 没传时，可临时回退 `GlobalAlloc / GlobalLock` 兼容老 DSM。

### 2.4 `TW_IMAGEINFO`

| 字段 | 说明 |
|---|---|
| `XResolution / YResolution` | FIX32，单位 DPI (`ICAP_UNITS = TWUN_INCHES`)。 |
| `ImageWidth / ImageLength` | 像素宽高。 |
| `SamplesPerPixel` | BW/灰度=1，RGB=3。 |
| `BitsPerSample[8]` | 每个样本 8 位（1-bit 也按 1 处理）。 |
| `BitsPerPixel` | BW=1, GRAY=8, RGB=24。 |
| `Planar` | 固定 `FALSE` (interleaved)。 |
| `PixelType` | `TWPT_BW / TWPT_GRAY / TWPT_RGB`。 |
| `Compression` | 固定 `TWCP_NONE`。 |

### 2.5 DPI 与 `biXPelsPerMeter`

`pixels_per_meter = dpi * 39.37`。`getImageInfo` 和 `allocAndFillDibHeader` 都从同一份 `ScannerSettings` 推算，保证两处一致。

## 3. 设计目标

- 100% 兼容 TWAIN 2.x 应用的默认 Native Transfer 流程。
- 与 File Transfer 共享同一个 `VirtualScanner` 图像生成管线。
- 严格使用 DSM 内存接口。
- DIB 结构经得起 Windows / 第三方图像库（XnView, IrfanView, Photoshop, NAPS2 等）的检查。
- 支持 1-bit / 8-bit / 24-bit。
- 支持 strip 模式读取像素（`getScanStrip`）作为内部抽象，便于将来加入 `TWSX_MEMORY`。

非目标：

- 不支持 `TWSX_MEMORY` 的分块返回。
- 不支持 16/32 位每样本（高 bit-depth 灰度或 HDR）。
- 不支持平面像素 (`Planar = TRUE`)。
- 不支持 Native Transfer 内的压缩（`Compression` 始终 `TWCP_NONE`）。
- Native Transfer 自身不写文件。

## 4. 总体设计

```text
TwainDataSource
├── handleDatImageNativeXfer()  // DG_IMAGE / DAT_IMAGENATIVEXFER / MSG_GET 入口
│     └── transfer()            // 把像素读到 image_data_ 缓冲
│           └── scanner_.getScanStrip()
│     └── getDibImage()         // 把 image_data_ 包成 DIB 句柄
│           ├── allocAndFillDibHeader()
│           └── copyDibPixelData()
│
├── handleDatImageInfo() / getImageInfo()
│
└── enableDs()                   // 触发 acquireImage() + MSG_XFERREADY

VirtualScanner
├── acquireImage()    // 加载下一张源图 (FreeImage)
├── preScanPrep()     // 24-bit 化、按页面/DPI 缩放、像素格式转换
├── applyDpiMetadata()// 设置 dots-per-meter + EXIF resolution
└── getScanStrip()    // 自底向上、行对齐输出
```

关键时序：

1. App `MSG_ENABLEDS` -> DS `enableDs()`：`acquireImage()` 完成后发 `MSG_XFERREADY`，state 5 -> 6。
2. App `DAT_IMAGEINFO / MSG_GET` -> DS `getImageInfo()` 用当前 `ScannerSettings` 和 DIB 推算 `TW_IMAGEINFO`。
3. App `DAT_IMAGENATIVEXFER / MSG_GET` -> DS：
   - 容错：state==5 且仍有 pending 时自动 promote 到 6；state==5 且没有 pending 时返回 `TWRC_CANCEL`。
   - `transfer()` 按 strip 把像素读入 `image_data_`。
   - `getDibImage()` 再分配 DIB 句柄，填 header + palette + 像素行。
   - state 6 -> 7，返回 `TWRC_XFERDONE`。
4. App `DAT_PENDINGXFERS / MSG_ENDXFER` -> `endXfer()`：清 pending，state 7 -> 5。

## 5. 重要决策和原因

### 5.1 在 `enableDs()` 里立即 `acquireImage()`

`MSG_XFERREADY` 之后应用通常会马上调 `DAT_IMAGEINFO`，需要确切的尺寸和位深。先解码可以保证响应准确，且与 File Transfer 保持一致状态边界。

### 5.2 通过 strip 接口 `getScanStrip()` 拷贝像素

`transfer()` 循环调用 `getScanStrip(buf, n, got)`，每次最多约 64000 字节并向下取整到整数行。保留 strip 抽象便于以后接入 `TWSX_MEMORY`，并落在历史上各类 DSM 都安全的边界内。

### 5.3 使用 DSM 内存接口而不是 `GlobalAlloc`

TWAIN 2.x 要求跨进程内存对象用 DSM 提供的分配器，应用端才能正确释放。封装在 `dsmAlloc / dsmFree / dsmLockMemory / dsmUnlockMemory` 中，缺少 `DAT_ENTRYPOINT` 时退回 `GlobalAlloc`。

### 5.4 BITMAPINFOHEADER + 调色板 + 像素三段分别 lock/unlock

DSM 的 `MemLock` 不保证返回稳定指针，多次 lock/unlock 在所有 DSM 上都安全；分段也使错误处理路径更清晰。

### 5.5 24-bit 像素直接按 BGR 传递

FreeImage 在 Windows 默认 BGR 排序，恰好与 DIB / BMP 一致，省一次全图遍历，对大图意义很大。

### 5.6 自底向上行序 + 4 字节对齐

DIB 规范要求行序自底向上、行字节 4 字节对齐。`BYTES_PERLINE(width, bpp)` 宏统一计算，避免每个调用点各算一遍。

### 5.7 `TW_IMAGEINFO` 现算现填

避免缓存与 `ScannerSettings` 不一致；计算成本极低。

### 5.8 兼容 "跳过 MSG_PROCESSEVENT" 的应用

某些早期工具会直接发 `DAT_IMAGENATIVEXFER / MSG_GET`。DS 内做兜底比让应用挂死或返回 `TWCC_SEQERROR` 更友好；无 pending 时返回 `TWRC_CANCEL`，符合规范。

## 6. 架构各组件改动点

### 6.1 `capability.cpp`

- `ICAP_XFERMECH` 默认 `TWSX_NATIVE`，可选 `{TWSX_NATIVE, TWSX_FILE}`。
- `ICAP_PIXELTYPE` 提供 `TWPT_BW / TWPT_GRAY / TWPT_RGB`。
- `ICAP_XRESOLUTION / ICAP_YRESOLUTION` 提供 150 / 200 / 300 / 600。
- `ICAP_UNITS = TWUN_INCHES`。
- `ICAP_PIXELFLAVOR = TWPF_CHOCOLATE`。
- `CAP_UICONTROLLABLE = TRUE`。

### 6.2 `twain_data_source.h / .cpp`

- 成员：`pending_xfers_`、`image_info_`、`image_data_`（DSM 分配的中间像素缓冲）、`canceled_`、`xfer_pending_`。
- 入口：`handleDatImageInfo / handleDatImageNativeXfer / handleDatPendingXfers`。
- 内部：`transfer / getDibImage / allocAndFillDibHeader / copyDibPixelData`。
- 状态：`enableDs` 5->6，`handleDatImageNativeXfer` 6->7，`endXfer` 7->5。
- DSM 内存胶层：`dsmAlloc / dsmFree / dsmLockMemory / dsmUnlockMemory`。

### 6.3 `virtual_scanner.h / .cpp`

- `acquireImage()` 用 FreeImage 加载源图。
- `preScanPrep()` -> `ensure24BitDib()` -> `applyPageSizeScaling()` -> `applyPixelFormat()` 保证 DIB 与 `ICAP_PIXELTYPE` 一致。
- `applyDpiMetadata()` 写 dots-per-meter，保证 `TW_IMAGEINFO` 与 DIB header 一致。
- `getScanStrip()` bottom-up 输出，每次返回若干完整行，自动补 4 字节对齐。

### 6.4 settings UI (`settings_server.cpp`)

- Native Transfer 是 UI 默认模式 (`transfer_mode = 0`)。
- UI 选择经 `enableDs()` 写回 `ICAP_PIXELTYPE / ICAP_XRESOLUTION / ICAP_YRESOLUTION`。
- File 模式相关字段在 Native 模式下隐藏。

### 6.5 DSM 接口胶层

- `callDsmEntry()` 动态加载 `TWAIN_32.dll` 并缓存 `DSM_Entry`。
- `setEntryPoints()` 处理 `DAT_ENTRYPOINT / MSG_SET`。
- 失败时 fall back 到 `GlobalAlloc / GlobalLock`。

## 7. 典型流程示例

### 7.1 ShowUI=TRUE 的 Native Transfer

```text
1. App: MSG_OPENDS -> state 4 -> 5
2. App: ICAP_PIXELTYPE / MSG_SET = TWPT_RGB
3. App: MSG_ENABLEDS, ShowUI=TRUE
   DS:  UI 弹出，用户选 RGB / 300 DPI / A4 -> Scan
        updateScannerFromCaps + acquireImage + preScanPrep
        发 MSG_XFERREADY, state 5 -> 6
4. App: DAT_EVENT / MSG_PROCESSEVENT
5. App: DAT_IMAGEINFO / MSG_GET
   DS:  返回 Width=2480, Height=3508, BPP=24, 300 DPI
6. App: DAT_IMAGENATIVEXFER / MSG_GET
   DS:  transfer + getDibImage -> TW_HANDLE, TWRC_XFERDONE, state 6 -> 7
7. App: MSG_ENDXFER -> MSG_DISABLEDS
```

### 7.2 ShowUI=FALSE 的 Native Transfer

```text
1. App: ICAP_PIXELTYPE / MSG_SET = TWPT_GRAY
       ICAP_XRESOLUTION / MSG_SET = 600
2. App: MSG_ENABLEDS, ShowUI=FALSE
   DS:  updateScannerFromCaps + acquireImage + preScanPrep
        发 MSG_XFERREADY
3. App: DAT_IMAGEINFO / MSG_GET -> PixelType=TWPT_GRAY, BPP=8, 600 DPI
4. App: DAT_IMAGENATIVEXFER / MSG_GET
   DS:  DIB 句柄含 256 项灰阶调色板 + 8-bit 像素
5. App: MSG_ENDXFER -> MSG_DISABLEDS
```

## 8. 限制

- `pending_xfers_.Count` 固定为 1。
- 一次性把整张 DIB 放到内存里：A4 @ 600 DPI 24-bit 大约 100 MB。
- 不支持 `Planar = TRUE` 与 `Compression != TWCP_NONE`。
- 不支持每样本 > 8 位。
- 没有 `DSM_MemAllocate` 时的 `GlobalAlloc` 回退路径，跨进程语义可能不同。
- strip 大小固定 ~64 KB，不暴露给应用。
- 自动 promote state 5 -> 6 是兼容性兜底，非规范行为。
- `BitsPerSample[8]` 只填到 `SamplesPerPixel` 项。
- 没有 strip 复制进度回调。

## 9. 下一步工作

- 评估超大图的内存压力，引入 `TWSX_MEMORY`。
- 支持每样本 > 8 位（16/48-bit）。
- strip 大小做成可配置（`DAT_SETUPMEMXFER` 或 settings UI）。
- 给 `TW_IMAGEINFO` 加 `XNativeResolution / YNativeResolution`。
- `transfer()` 增加进度回调，UI 上显示。
- 加入自动化测试：stub DSM + 标准图像比对 DIB 位精确度。
- 在 `closeDs` 之外补一次 `image_data_` 的安全释放。
- 验证不同 FreeImage 版本是否始终 BGR；必要时运行时探测。
- 给 `GlobalAlloc` 回退路径增加日志。

</details>

<details>
<summary>English</summary>

## 1. Requirement

The virtual scanner must support TWAIN's default Native Transfer mode (`ICAP_XFERMECH = TWSX_NATIVE`): the DS assembles the entire image into a single DIB block and hands it to the application as a TWAIN handle in one shot.

Main requirements:

- `ICAP_XFERMECH` defaults to `TWSX_NATIVE`; an app that performs no capability negotiation must still scan via Native Transfer.
- Support `DG_IMAGE / DAT_IMAGENATIVEXFER / MSG_GET`, returning a `TW_HANDLE` lockable with `GlobalLock` / `DSM_MemLock`.
- DIB content = `BITMAPINFOHEADER` + palette (if applicable) + pixel data; bottom-up rows; each row padded to 4 bytes.
- Support 1-bit (BW), 8-bit (grayscale), and 24-bit (RGB):
  - 1-bit with a 2-entry palette.
  - 8-bit with a 256-entry grayscale palette.
  - 24-bit without palette; BGR pixel order.
- `TW_IMAGEINFO` must match the DIB exactly.
- Memory must come from the DSM's allocator; fall back to `GlobalAlloc / GlobalLock` only when the DSM did not supply one.
- DIB's `biXPelsPerMeter / biYPelsPerMeter` and `TW_IMAGEINFO.XResolution / YResolution` must stay in sync.
- `ICAP_UNITS = TWUN_INCHES`.
- Follow the TWAIN state machine: `kEnabled (5) -> kXferReady (6) -> kXferring (7) -> kEnabled (5)`.

Non-functional:

- Flatbed only; `pending_xfers_.Count` fixed at 1.
- Large images must work; no intermediate buffer can fail its single allocation.
- Share `acquireImage` + `preScanPrep` with File Transfer.

## 2. Domain knowledge

### 2.1 Protocol sequence

```text
1. MSG_ENABLEDS -> acquireImage(), MSG_XFERREADY (5 -> 6)
2. DAT_EVENT / MSG_PROCESSEVENT
3. DAT_IMAGEINFO / MSG_GET
4. DAT_IMAGENATIVEXFER / MSG_GET -> TW_HANDLE, TWRC_XFERDONE (6 -> 7)
5. DAT_PENDINGXFERS / MSG_ENDXFER (7 -> 5)
6. MSG_DISABLEDS (5 -> 4)
```

Return codes: `TWRC_XFERDONE`, `TWRC_CANCEL`, `TWCC_SEQERROR`, `TWCC_LOWMEMORY`.

### 2.2 DIB layout

`BITMAPINFOHEADER` -> palette -> pixel data, bottom-up, 4-byte row alignment. 24-bit pixels are BGR. `RGBQUAD` is `{B, G, R, reserved}`.

### 2.3 DSM memory interface

TWAIN 2.4+ supplies `DSM_MemAllocate / Free / Lock / Unlock` via `DAT_ENTRYPOINT / MSG_SET`. Use them for any handle returned to the app; fall back to `GlobalAlloc / GlobalLock` for legacy DSMs.

### 2.4 `TW_IMAGEINFO`

`XResolution / YResolution` (FIX32 DPI), `ImageWidth / ImageLength`, `SamplesPerPixel` (1 or 3), `BitsPerSample[8]` (8 per sample), `BitsPerPixel` (1/8/24), `Planar = FALSE`, `PixelType`, `Compression = TWCP_NONE`.

### 2.5 DPI vs `biXPelsPerMeter`

`pixels_per_meter = dpi * 39.37`. `getImageInfo` and `allocAndFillDibHeader` both derive from the same `ScannerSettings`.

## 3. Design goals

- 100% compatibility with default Native Transfer for TWAIN 2.x apps.
- Share `VirtualScanner` pipeline with File Transfer.
- Strict DSM memory ownership.
- DIB layout that passes Windows and third-party image-library inspection.
- 1-bit / 8-bit / 24-bit support.
- Strip-based pixel reads as the internal abstraction.

Non-goals: `TWSX_MEMORY`, high-bit-depth pixels, `Planar = TRUE`, native-side compression, file writes.

## 4. Overall design

```text
TwainDataSource
├── handleDatImageNativeXfer()  // protocol entry
│     └── transfer()            // strip-based copy into image_data_
│     └── getDibImage()         // wrap into DIB handle
│           ├── allocAndFillDibHeader()
│           └── copyDibPixelData()
├── handleDatImageInfo() / getImageInfo()
└── enableDs()                   // acquireImage + MSG_XFERREADY

VirtualScanner
├── acquireImage()
├── preScanPrep()
├── applyDpiMetadata()
└── getScanStrip()
```

Key timing:

1. `MSG_ENABLEDS` -> `acquireImage()` -> `MSG_XFERREADY`, state 5 -> 6.
2. `DAT_IMAGEINFO / MSG_GET` -> `getImageInfo()`.
3. `DAT_IMAGENATIVEXFER / MSG_GET` -> `transfer()` + `getDibImage()`, state 6 -> 7, `TWRC_XFERDONE`.
4. `DAT_PENDINGXFERS / MSG_ENDXFER` -> state 7 -> 5.

## 5. Key decisions and rationale

### 5.1 `acquireImage()` runs immediately in `enableDs()`

Apps call `DAT_IMAGEINFO` right after `MSG_XFERREADY` and need accurate dimensions/bit depth. Decoding up front keeps the state boundary identical to File Transfer.

### 5.2 Strip-based pixel reads via `getScanStrip()`

`transfer()` loops on ~64000-byte strips (rounded to whole rows). Mirrors real scanners, makes future `TWSX_MEMORY` straightforward, and stays within historically safe DSM bounds.

### 5.3 Prefer DSM memory functions over `GlobalAlloc`

Cross-process memory must come from the DSM allocator. Centralized in a small shim that falls back to `GlobalAlloc` for legacy DSMs.

### 5.4 Separate lock/unlock for header, palette, and pixels

`MemLock` does not guarantee stable pointers across calls. Multiple lock/unlock pairs are safe across DSMs and keep error handling localized.

### 5.5 Skip R/B swap for 24-bit pixels

FreeImage uses BGR on Windows, matching DIB. Skipping a swap saves a full-image pass (about 100 MB at A4 @ 600 DPI).

### 5.6 Bottom-up rows + 4-byte alignment

Matches Windows DIB convention; misalignment causes downstream readers to misinterpret subsequent rows. `BYTES_PERLINE` centralizes the alignment math.

### 5.7 Compute `TW_IMAGEINFO` on demand

Avoids cache vs state drift; cost is trivial.

### 5.8 Tolerate apps that skip `MSG_PROCESSEVENT`

Auto-promote state 5 -> 6 when pending > 0, return `TWRC_CANCEL` when pending == 0. More forgiving than failing the call.

## 6. Component changes

### 6.1 `capability.cpp`

- `ICAP_XFERMECH` default `TWSX_NATIVE` with choices `{TWSX_NATIVE, TWSX_FILE}`.
- `ICAP_PIXELTYPE`: `TWPT_BW / TWPT_GRAY / TWPT_RGB`.
- `ICAP_XRESOLUTION / ICAP_YRESOLUTION`: 150 / 200 / 300 / 600.
- `ICAP_UNITS = TWUN_INCHES`.
- `ICAP_PIXELFLAVOR = TWPF_CHOCOLATE`.
- `CAP_UICONTROLLABLE = TRUE`.

### 6.2 `twain_data_source.h / .cpp`

- Members: `pending_xfers_`, `image_info_`, `image_data_` (DSM-allocated intermediate pixel buffer), `canceled_`, `xfer_pending_`.
- Entries: `handleDatImageInfo`, `handleDatImageNativeXfer`, `handleDatPendingXfers`.
- Helpers: `transfer`, `getDibImage`, `allocAndFillDibHeader`, `copyDibPixelData`.
- State transitions: `enableDs` 5->6, `handleDatImageNativeXfer` 6->7, `endXfer` 7->5.
- DSM memory shim: `dsmAlloc / dsmFree / dsmLockMemory / dsmUnlockMemory`.

### 6.3 `virtual_scanner.h / .cpp`

- `acquireImage()`: FreeImage loader.
- `preScanPrep()` chain to ensure pixel type matches `ICAP_PIXELTYPE`.
- `applyDpiMetadata()` writes dots-per-meter so `TW_IMAGEINFO` and DIB header agree.
- `getScanStrip()` bottom-up + 4-byte padded.

### 6.4 settings UI (`settings_server.cpp`)

- Native is the default `transfer_mode = 0`.
- UI choices flow into `ICAP_PIXELTYPE / ICAP_XRESOLUTION / ICAP_YRESOLUTION`.
- File-mode fields are hidden in Native mode.

### 6.5 DSM interface shim

- `callDsmEntry()` dynamic-loads `TWAIN_32.dll` and caches `DSM_Entry`.
- `setEntryPoints()` stores DSM memory function pointers.
- Falls back to `GlobalAlloc / GlobalLock` for legacy DSMs.

## 7. Typical flows

### 7.1 Native Transfer with ShowUI=TRUE

```text
1. MSG_OPENDS -> state 4 -> 5
2. ICAP_PIXELTYPE / MSG_SET = TWPT_RGB
3. MSG_ENABLEDS, ShowUI=TRUE
   DS: UI -> RGB / 300 DPI / A4 -> Scan
       acquireImage + preScanPrep
       MSG_XFERREADY, state 5 -> 6
4. DAT_EVENT / MSG_PROCESSEVENT
5. DAT_IMAGEINFO / MSG_GET -> 2480x3508, BPP=24, 300 DPI
6. DAT_IMAGENATIVEXFER / MSG_GET -> TW_HANDLE, TWRC_XFERDONE, 6 -> 7
7. MSG_ENDXFER -> MSG_DISABLEDS
```

### 7.2 Native Transfer with ShowUI=FALSE

```text
1. ICAP_PIXELTYPE / MSG_SET = TWPT_GRAY
   ICAP_XRESOLUTION / MSG_SET = 600
2. MSG_ENABLEDS, ShowUI=FALSE
   DS: acquireImage + preScanPrep, MSG_XFERREADY
3. DAT_IMAGEINFO / MSG_GET -> PixelType=TWPT_GRAY, BPP=8, 600 DPI
4. DAT_IMAGENATIVEXFER / MSG_GET -> DIB with 256-entry grayscale palette
5. MSG_ENDXFER -> MSG_DISABLEDS
```

## 8. Limitations

- `pending_xfers_.Count` fixed at 1.
- Whole image held in memory (about 100 MB for A4 @ 600 DPI 24-bit).
- No `Planar = TRUE`, no compression.
- No high-bit-depth pixel types.
- `GlobalAlloc` fallback has different cross-process semantics from `DSM_MemAllocate`.
- Strip size fixed at ~64 KB and not exposed to the app.
- Auto-promote 5 -> 6 is a compatibility shim, non-standard.
- `BitsPerSample[8]` is filled only up to `SamplesPerPixel`.
- No progress callback during strip copy.

## 9. Next steps

- Add `TWSX_MEMORY` for very large scans.
- Support high-bit-depth pixel types (16-bit grayscale, 48-bit RGB).
- Expose strip size (e.g. via `DAT_SETUPMEMXFER` or settings UI).
- Populate `XNativeResolution / YNativeResolution`.
- Add a transfer progress callback for the settings UI.
- Add automated tests with a stub DSM and bit-exact DIB comparisons.
- Defensive `image_data_` cleanup outside `closeDs`.
- Runtime-detect FreeImage pixel order instead of relying on a comment.
- Log when falling back to `GlobalAlloc` so legacy-DSM memory issues are easier to diagnose.

</details>
