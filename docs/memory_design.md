# Memory Transfer Mode Design

Design notes for the TWAIN Memory Transfer (`TWSX_MEMORY`) path in BN Tech Virtual Scanner.

<details open>
<summary>中文说明</summary>

## 1. 需求

虚拟扫描仪需要支持 TWAIN 的 Memory Transfer 模式 (`ICAP_XFERMECH = TWSX_MEMORY`)：图像不再一次性以一个 DIB 句柄返回，而是按"条带"(strip) 分多次拷贝进 **应用预分配** 的内存缓冲区，直到整张图传输完毕。

主要需求：

- `ICAP_XFERMECH` 必须把 `TWSX_MEMORY` 加入支持列表 (`{TWSX_NATIVE, TWSX_FILE, TWSX_MEMORY}`)；默认仍为 `TWSX_NATIVE`。
- 支持 `DG_CONTROL / DAT_SETUPMEMXFER / MSG_GET`，返回 `MinBufSize / MaxBufSize / Preferred` 三个缓冲尺寸建议。
- 支持 `DG_IMAGE / DAT_IMAGEMEMXFER / MSG_GET`，每次把一组 **完整扫描行** 写入应用提供的 `TW_MEMORY` 缓冲，并填写 `TW_IMAGEMEMXFER` 元数据。
- 像素字节序必须符合 TWAIN 规范：`TWPT_RGB` 在条带里按 **R, G, B** 顺序，而不是 Windows DIB 的 BGR。
- 行序为 **自顶向下** (top-down)：第一次返回的条带包含图像最上面若干行；`YOffset` 单调递增。
- `Compression = TWCP_NONE`，未压缩裸像素。
- 整图完成时返回 `TWRC_XFERDONE`，未完成时返回 `TWRC_SUCCESS`。
- 状态机：`kEnabled (5) -> kXferReady (6) -> kXferring (7) -> kEnabled (5)`，与 Native/File 路径一致。
- 复用现有 `VirtualScanner` 与 `transfer()` 管线，**改动最小**。

非功能性需求：

- 与 Native / File 模式共用 `acquireImage` + `preScanPrep` + `getScanStrip` 管线。
- 在 32 位与 64 位 DS 上行为一致。
- 单条带大小由应用决定，DS 必须容忍各种缓冲尺寸（典型 4 KB – 1 MB），并自动按整行向下取整。
- 大图（A4 600 DPI 24-bit ≈ 100 MB）必须能正常分多次传输，且 R/B 翻转开销可接受。

## 2. 领域知识

### 2.1 Memory Transfer 协议序列

```text
1. App: DG_CONTROL / DAT_USERINTERFACE / MSG_ENABLEDS
   DS:  acquireImage(), MSG_XFERREADY (state 5 -> 6)
2. App: DG_CONTROL / DAT_EVENT / MSG_PROCESSEVENT
3. App: DG_IMAGE   / DAT_IMAGEINFO / MSG_GET
4. App: DG_CONTROL / DAT_SETUPMEMXFER / MSG_GET
        -> 取得 Min/Max/Preferred 缓冲尺寸
5. App: 按 Preferred 分配自己的 TW_MEMORY 缓冲
6. 循环:
   App: DG_IMAGE / DAT_IMAGEMEMXFER / MSG_GET (TW_MEMORY.TheMem 已分配)
   DS:  把下一段整行像素拷贝进 TheMem，填 TW_IMAGEMEMXFER
        中间条带返回 TWRC_SUCCESS；最后一条带返回 TWRC_XFERDONE
        state 第一次进入时 6 -> 7
7. App: DG_CONTROL / DAT_PENDINGXFERS / MSG_ENDXFER   (state 7 -> 5)
8. App: DG_CONTROL / DAT_USERINTERFACE / MSG_DISABLEDS (state 5 -> 4)
```

返回码：

- 还有数据：`TWRC_SUCCESS`。
- 已传完最后一条带：`TWRC_XFERDONE`。
- 应用取消：`TWRC_CANCEL`。
- 状态错乱：`TWRC_FAILURE + TWCC_SEQERROR`。
- 缓冲小于一行：`TWRC_FAILURE + TWCC_BADVALUE`。
- 没有 pending：`TWRC_CANCEL`（自动取消，方便应用走出循环）。

### 2.2 `TW_SETUPMEMXFER`

```c
struct TW_SETUPMEMXFER {
  TW_UINT32 MinBufSize;   // DS 能接受的最小缓冲
  TW_UINT32 MaxBufSize;   // DS 能接受的最大缓冲
  TW_UINT32 Preferred;    // DS 推荐的最优缓冲
};
```

应用通常按 `Preferred` 分配；DS 必须容忍任何在 `[MinBufSize, MaxBufSize]` 区间内的尺寸。

### 2.3 `TW_IMAGEMEMXFER`

```c
struct TW_IMAGEMEMXFER {
  TW_UINT16 Compression;   // TWCP_NONE / TWCP_GROUP4 / TWCP_JPEG ...
  TW_UINT32 BytesPerRow;   // 单行字节数（含填充）
  TW_UINT32 Columns;       // 本条带的像素宽度（通常 = 整图宽度）
  TW_UINT32 Rows;          // 本条带包含的扫描行数
  TW_UINT32 XOffset;       // 本条带左上角 X 像素偏移
  TW_UINT32 YOffset;       // 本条带左上角 Y 像素偏移（top-down）
  TW_UINT32 BytesWritten;  // DS 本次实际写入字节数
  TW_MEMORY Memory;        // 应用提供的目标缓冲
};
struct TW_MEMORY {
  TW_UINT32 Flags;
  TW_UINT32 Length;        // 缓冲容量
  TW_MEMREF TheMem;        // 应用提供的指针
};
```

### 2.4 行序与像素字节序

- **行序**：TWAIN Memory 模式与文件相同，使用 **top-down**。第一条带包含图像最上面的行，`YOffset` 从 0 开始单调递增。这一点与 Windows DIB 的 bottom-up 完全相反。
- **字节序**：TWAIN `TWPT_RGB` 在 Memory 模式下要求 R-G-B 顺序；Windows DIB 与 FreeImage 在 Windows 上为 B-G-R。所以 24-bit 数据必须做 R/B 交换。
- **行对齐**：与 DIB 相同的 4 字节对齐 (`BytesPerRow = ((Columns * BitsPerPixel) + 31) / 32 * 4`)，方便应用复用 DIB 解析代码。

### 2.5 条带切分

- DS 每次 `DAT_IMAGEMEMXFER` 调用只能写入 **整数行**：`rows = min(app_buf_size, remaining_bytes) / BytesPerRow`。
- 应用缓冲必须至少装得下一行 (`>= BytesPerRow`)；否则 DS 返回 `TWRC_FAILURE + TWCC_BADVALUE`，由应用扩大缓冲后重试。
- DS 不允许把同一行拆到两个条带，因为应用通常按 `Rows * BytesPerRow` 边界做后续处理。

### 2.6 与 Native Transfer 的关系

Native Transfer 已经把整图按行存到 `image_data_` 中（`transfer()` 函数），所以 Memory Transfer 完全可以复用这个缓冲，只是把它**切片**送给应用即可，无需重新设计像素生成路径。

## 3. 设计目标

- 100% 兼容 TWAIN 2.x 应用的 Memory Transfer 流程（Twack 32、NAPS2、Dynamic Web TWAIN 等）。
- 复用 `transfer()` 把整图灌进 `image_data_`，避免再写一份"流式从扫描器拉一段"的代码。
- 代码改动最小：只新增 2 个 handler、1 个偏移成员、capability 列表追加 1 项；不动 `VirtualScanner`、不动 settings UI 服务端。
- 单一像素字节序入口：只在 Memory 输出处理 R/B 翻转；Native / File 路径不受影响。
- 与 Native / File 共用同一套状态机与 `pending_xfers_` 计数。

非目标：

- 不支持压缩 (`Compression = TWCP_NONE` only)；不实现 `TWCP_GROUP4` / `TWCP_JPEG` 等。
- 不实现"流式"按需解码（图像仍然一次性 `transfer()` 进 `image_data_`）。
- 不支持每样本 > 8 位。
- 不动态调整 `MinBufSize / MaxBufSize / Preferred`，使用固定值。
- 不暴露条带大小给 settings UI。

## 4. 总体设计

```text
TwainDataSource
├── handleDatSetupMemXfer()       // DAT_SETUPMEMXFER / MSG_GET
│                                 // 返回 8 KB / 256 KB / 64 KB
│
├── handleDatImageMemXfer()       // DAT_IMAGEMEMXFER / MSG_GET
│     ├── (state 5->6 自动 promote)
│     ├── transfer()              // 首次调用：把整图灌入 image_data_
│     ├── 切片                    // chunk = min(app_buf, remaining)
│     │                           // rows = chunk / BytesPerRow
│     ├── memcpy image_data_[off..] -> app buffer
│     ├── 24-bit RGB: 行内 R/B 翻转
│     └── 填 TW_IMAGEMEMXFER + 返回 TWRC_SUCCESS / TWRC_XFERDONE
│
└── 复用：
      ├── transfer()               // 已存在，Native 模式同款
      ├── getImageInfo()
      ├── endXfer / resetXfer      // 同时重置 mem_xfer_offset_
      └── DSM 内存接口 (dsmLockMemory / dsmUnlockMemory)
```

数据流：

```text
FreeImage DIB (BGR, bottom-up internal)
        │
        ▼ getScanStrip() bottom-up 索引翻转 -> visually top-down
image_data_  (BGR, top-down, 4-byte aligned)
        │
        ├──> Native: copyDibPixelData() 再翻一次行 -> DIB bottom-up (BGR ok)
        └──> Memory: memcpy 行 -> 行内 R/B swap -> 应用缓冲 (RGB, top-down)
```

关键时序：

1. App `MSG_ENABLEDS` -> DS `enableDs()`：`acquireImage()` 完成后发 `MSG_XFERREADY`，state 5 -> 6。
2. App `DAT_SETUPMEMXFER / MSG_GET` -> DS 返回 `{8 KB, 256 KB, 64 KB}`。
3. App `DAT_IMAGEMEMXFER / MSG_GET`（首次）：
   - DS 调用 `transfer()`，整图灌入 `image_data_`。
   - `mem_xfer_offset_ = 0`，state 6 -> 7。
   - 算 `rows`，memcpy，R/B 翻转，填 metadata。
   - `mem_xfer_offset_ += bytes`，返回 `TWRC_SUCCESS`（或 `TWRC_XFERDONE` 如果一次就完）。
4. App 重复 `DAT_IMAGEMEMXFER`：从 `image_data_[mem_xfer_offset_..]` 继续切片。
5. 最后一次：`mem_xfer_offset_ >= total`，返回 `TWRC_XFERDONE`。
6. App `MSG_ENDXFER` -> `endXfer()`：`mem_xfer_offset_ = 0`，state 7 -> 5。

## 5. 重要决策和原因

### 5.1 复用 `transfer()` 把整图灌进 `image_data_`，再切片

- **决策**：Memory Transfer 第一次调用时执行原本 Native 用的 `transfer()`，把整图加载到 `image_data_`，后续条带只是 `memcpy` 切片。
- **原因**：
  - 改动最小：`transfer()` 已经处理了 FreeImage 加载、像素格式转换、4 字节对齐、bottom-up 翻转等所有细节。
  - 行为可预测：`TW_IMAGEINFO` 已在 `MSG_XFERREADY` 时算好，应用拿到的尺寸与切片完全一致，不会因延迟解码导致尺寸漂移。
  - 节省工程时间：避免维护两套相似但不完全相同的像素生成路径。
- **代价**：内存占用同 Native（A4 600 DPI 24-bit ≈ 100 MB）。Memory 模式的"省内存"优势没拿到，但 TWAIN 应用拿 Memory 模式更多是为了"分块拿到 byte buffer 立刻处理"，而不是为了 DS 端省内存。

### 5.2 固定的 `MinBufSize / Preferred / MaxBufSize`

- **决策**：8 KB / 64 KB / 256 KB 三个固定值。
- **原因**：
  - 64 KB 与内部 `transfer()` 的 64000-byte strip 边界相近，应用按 Preferred 分配时几乎所有图都能在合理次数内传完。
  - 不依赖 `image_info_` 已就绪，`DAT_SETUPMEMXFER` 可以在任何状态稳定回答。
  - 简单可预期，应用调试时不会被动态值困扰。
- **代价**：对极小图（1 行也只有几 KB）应用一次能拿完，但应用仍按 64 KB 分配，浪费内存；可接受。

### 5.3 24-bit 时 **R/B 翻转**；8-bit / 1-bit 不翻

- **决策**：仅对 `BitsPerPixel == 24` 的条带做 R/B 字节交换。
- **原因**：
  - TWAIN 规范要求 `TWPT_RGB` 在 Memory 模式按 R-G-B 字节序；FreeImage 在 Windows 上为 BGR；直接 memcpy 会让 Twack 32 / NAPS2 等显示成蓝/红互换。
  - 灰度（8-bit）和黑白（1-bit）没有通道概念，不需要翻转。
  - 翻转只在条带写入后做，每次只对当前条带的若干行操作，O(rows * width)；A4 600 DPI 24-bit 整图约 25 M pixel，整体翻转代价远小于一次磁盘 I/O。
- **代价**：对每条带多遍历一次像素。可接受。

### 5.4 不做"行不够大就拒绝"以外的兜底

- **决策**：当应用缓冲小于一行 (`< BytesPerRow`) 时返回 `TWRC_FAILURE + TWCC_BADVALUE`，由应用扩大缓冲后重试。
- **原因**：
  - 如果允许半行写入，会破坏 `Rows * BytesPerRow == BytesWritten` 这一应用普遍依赖的不变量。
  - 应用很容易处理 `TWCC_BADVALUE` 后用 `Preferred` 重新分配。

### 5.5 复用 state 5 -> 6 的自动 promote

- **决策**：当应用直接发 `DAT_IMAGEMEMXFER` 而没经 `MSG_PROCESSEVENT` 时，若 `pending_xfers_.Count != 0`，state 自动 5 -> 6 再 6 -> 7。
- **原因**：与 Native Transfer 一致；Twack 等测试工具确实存在跳过事件循环直接拉数据的用法。

### 5.6 `mem_xfer_offset_` 作为唯一切片状态

- **决策**：用一个 `TW_UINT32 mem_xfer_offset_` 表示"已交付字节数"，由 `mem_xfer_offset_ % BytesPerRow == 0` 保证总在行边界。
- **原因**：
  - 单一变量，状态最少，易推理。
  - `YOffset = mem_xfer_offset_ / BytesPerRow` 直接算出，不再需要"已交付行数"重复跟踪。
  - 在 `endXfer / resetXfer / 构造函数`三处统一重置即可。

### 5.7 不实现压缩

- **决策**：始终 `Compression = TWCP_NONE`。
- **原因**：实现 G4/JPEG 需要引入额外编码路径与状态机，与 "代码尽量少改动" 目标不符。Memory 模式的主要用户场景（实时图像处理、缩略图）通常也是 RAW。

### 5.8 像素字节序在 Memory 输出处理，而不是改 `image_data_` 本身

- **决策**：保持 `image_data_` 为 BGR（Native 不变），仅在 Memory 写出口做行内交换。
- **原因**：
  - Native 路径要 BGR，File 路径靠 FreeImage 自动处理，只有 Memory 需要 RGB。
  - 如果改 `image_data_` 为 RGB，Native 又得在 `copyDibPixelData` 翻一遍，反而增加复杂度。
  - 把"协议特有的字节序"约束限制在协议出口，是单一职责原则。

## 6. 架构各组件改动点

### 6.1 `src/capability.cpp`

- `ICAP_XFERMECH` 的 supported_values 追加 `TWSX_MEMORY`：

```cpp
addCap(ICAP_XFERMECH, TWTY_UINT16, TWON_ONEVALUE, kCapAll, TWSX_NATIVE,
       {TWSX_NATIVE, TWSX_FILE, TWSX_MEMORY});
```

默认值仍为 `TWSX_NATIVE`，向后兼容。

### 6.2 `src/twain_data_source.h`

- 新增 2 个 handler 声明：
  ```cpp
  TW_INT16 handleDatSetupMemXfer(TW_UINT16 msg, pTW_SETUPMEMXFER data);
  TW_INT16 handleDatImageMemXfer(TW_UINT16 msg, pTW_IMAGEMEMXFER data);
  ```
- 新增 1 个成员变量：
  ```cpp
  TW_UINT32 mem_xfer_offset_;  // 字节偏移，行边界对齐
  ```

### 6.3 `src/twain_data_source.cpp`

- 构造函数初始化 `mem_xfer_offset_(0)`。
- DG_CONTROL switch 中追加 `case DAT_SETUPMEMXFER:` 派发。
- DG_IMAGE switch 中追加 `case DAT_IMAGEMEMXFER:` 派发。
- `endXfer()` / `resetXfer()` 末尾追加 `mem_xfer_offset_ = 0;`。
- 实现 `handleDatSetupMemXfer`：固定返回 `{8 KB, 256 KB, 64 KB}`。
- 实现 `handleDatImageMemXfer`：
  - 校验 `msg == MSG_GET`、`data != nullptr`、`Memory.TheMem != nullptr`、`Memory.Length > 0`。
  - state 5 + pending > 0 时自动 promote 到 6。
  - 首次 (state == kXferReady)：调 `transfer()`，重置 `mem_xfer_offset_ = 0`，state -> kXferring。
  - 计算 `bpr / total`，若 `mem_xfer_offset_ >= total`：返回 0 行 + `TWRC_XFERDONE`。
  - 否则计算 `rows = min(app_buf, remaining) / bpr`；为 0 返回 `TWCC_BADVALUE`。
  - `dsmLockMemory(image_data_)` -> `memcpy` -> 24-bit R/B 翻转 -> `dsmUnlockMemory`。
  - 填 `Compression / BytesPerRow / Columns / Rows / XOffset / YOffset / BytesWritten`。
  - 推进 `mem_xfer_offset_`，最后一段返回 `TWRC_XFERDONE`，否则 `TWRC_SUCCESS`。

### 6.4 不动的组件

- `VirtualScanner`：完全不动，继续靠 `getScanStrip` 输出。
- `settings_server.cpp`：UI 不暴露 Memory 模式选项，由应用自己设 `ICAP_XFERMECH`。
- `ds_entry.cpp`：派发路径保持不变（仍是 `TwainDataSource::dsEntry`）。
- DSM 内存胶层：复用现有 `dsmAlloc / dsmFree / dsmLockMemory / dsmUnlockMemory`。

### 6.5 构建系统

- 无新文件、无新依赖；现有 `CMakeLists.txt` 自动编译变更。
- 32 位 / 64 位双构建均通过验证。

## 7. 典型流程示例

### 7.1 Twack 32 的 Memory Transfer 流程

```text
1. App: MSG_OPENDS -> state 4 -> 5
2. App: ICAP_XFERMECH / MSG_SET = TWSX_MEMORY
3. App: MSG_ENABLEDS, ShowUI=FALSE
   DS:  acquireImage + preScanPrep, MSG_XFERREADY, state 5 -> 6
4. App: DAT_IMAGEINFO / MSG_GET -> Width=2480, Height=3508, BPP=24
5. App: DAT_SETUPMEMXFER / MSG_GET -> {8K, 256K, 64K}
6. App: 分配 64 KB 缓冲
7. Loop (n 次):
   App: DAT_IMAGEMEMXFER / MSG_GET (TheMem=64KB)
   DS:  首次: transfer() 整图入 image_data_; state 6 -> 7
        切片: rows = 64*1024 / (2480*3) = 8 行
        memcpy 8 行 -> RGB 翻转 -> 填 metadata
        YOffset=0, 8, 16, ...; 最后一帧 TWRC_XFERDONE
8. App: DAT_PENDINGXFERS / MSG_ENDXFER -> state 7 -> 5
9. App: MSG_DISABLEDS -> state 5 -> 4
```

### 7.2 灰度图的 Memory Transfer

```text
1. App: ICAP_PIXELTYPE / MSG_SET = TWPT_GRAY
       ICAP_XFERMECH / MSG_SET = TWSX_MEMORY
2. App: MSG_ENABLEDS, ShowUI=FALSE
3. App: DAT_IMAGEINFO -> PixelType=TWPT_GRAY, BPP=8
4. App: DAT_SETUPMEMXFER -> {8K, 256K, 64K}
5. App: DAT_IMAGEMEMXFER 循环
   DS:  8-bit，不做 R/B 翻转；直接 memcpy
        rows = 64K / BytesPerRow
6. App: MSG_ENDXFER -> MSG_DISABLEDS
```

### 7.3 应用缓冲过小的兜底

```text
App: DAT_IMAGEMEMXFER, TheMem.Length = 100 字节
DS:  bpr = 7440 (2480 * 3, 4字节对齐)
     rows = 100 / 7440 = 0
     return TWRC_FAILURE + TWCC_BADVALUE
App: 重新按 Preferred 64 KB 分配，重试 -> 成功
```

## 8. 限制

- 全图一次性灌入 `image_data_`，没有真正的"按需流式"内存优势；A4 600 DPI 24-bit 仍占约 100 MB。
- 仅 `Compression = TWCP_NONE`；不支持 G4 / JPEG / RLE 等压缩条带。
- 不支持每样本 > 8 位（16-bit 灰度、48-bit RGB）。
- `MinBufSize / MaxBufSize / Preferred` 固定值，不随分辨率或 PixelType 调整。
- `mem_xfer_offset_` 是单调递增，没有"重发上一条带"的机制；应用一旦取走条带不能回滚。
- 自动 state 5 -> 6 promote 与 Native 共享，非严格规范行为。
- 没有 strip 复制进度回调，settings UI 无法显示百分比。
- 24-bit R/B 翻转使用 in-place 行内交换，对每条带额外一遍 O(rows * width) CPU；可被 SSE/AVX 优化但目前未做。
- `DAT_IMAGEMEMXFER` 的 `Memory.Flags`（应用提示 DS 该缓冲是否在指针/句柄/可执行段等）未做检查，统一当作有效指针。

## 9. 下一步工作

- 验证更多应用：NAPS2、Dynamic Web TWAIN、ScandAll PRO、ImageGear 的 Memory 模式 round-trip。
- 支持压缩条带：至少 `TWCP_GROUP4`（黑白）与 `TWCP_JPEG`（彩色），减少传输总字节。
- 支持每样本 16 / 48 位以支持 HDR 灰度 / RGB。
- 把 `MinBufSize / Preferred / MaxBufSize` 改成与图像尺寸联动（如 Preferred = 4 * BytesPerRow）。
- 加 strip 复制进度回调，在 settings UI 显示百分比。
- 引入"边解码边传"的真正流式管线，可去掉 `image_data_` 全图缓冲，省内存。
- 用 SSE/AVX 优化 24-bit R/B 翻转；或在 `transfer()` 输出时直接写 RGB 给 Memory 路径。
- 在 settings UI 暴露 "default transfer mode" 设置，方便人工测试 Memory 模式。
- 加自动化测试：stub DSM + 字节精确比对 Native 与 Memory 路径的输出像素一致。
- 评估并实现 `TWMR_INVERT` / `TWMR_DUAL` 等内存请求模式（当前实现仅支持单缓冲循环）。

</details>

<details>
<summary>English</summary>

## 1. Requirement

The virtual scanner must support TWAIN's Memory Transfer mode (`ICAP_XFERMECH = TWSX_MEMORY`): instead of returning the entire image as one DIB handle, the DS copies the image strip-by-strip into a buffer **the application pre-allocates**, until the whole image has been delivered.

Main requirements:

- `ICAP_XFERMECH` must add `TWSX_MEMORY` to its supported list (`{TWSX_NATIVE, TWSX_FILE, TWSX_MEMORY}`); the default remains `TWSX_NATIVE`.
- Support `DG_CONTROL / DAT_SETUPMEMXFER / MSG_GET`, returning `MinBufSize / MaxBufSize / Preferred`.
- Support `DG_IMAGE / DAT_IMAGEMEMXFER / MSG_GET`, copying whole-scanline chunks into the app-supplied `TW_MEMORY` buffer and filling `TW_IMAGEMEMXFER`.
- Pixel byte order must follow TWAIN spec: `TWPT_RGB` must be delivered as R-G-B, not Windows DIB's B-G-R.
- Row order must be top-down: first strip contains the topmost rows; `YOffset` increases monotonically.
- `Compression = TWCP_NONE` (raw uncompressed pixels).
- Return `TWRC_XFERDONE` on the last strip, `TWRC_SUCCESS` otherwise.
- State machine: `kEnabled (5) -> kXferReady (6) -> kXferring (7) -> kEnabled (5)`, identical to Native / File.
- Reuse the existing `VirtualScanner` and `transfer()` pipeline with minimal code changes.

Non-functional:

- Share `acquireImage + preScanPrep + getScanStrip` with Native / File.
- Identical behavior on 32-bit and 64-bit DS.
- Per-strip size is chosen by the application; the DS must tolerate any buffer (typically 4 KB – 1 MB) and silently round down to whole rows.
- Large images (A4 600 DPI 24-bit ≈ 100 MB) must transfer correctly with acceptable R/B-swap overhead.

## 2. Domain knowledge

### 2.1 Protocol sequence

```text
1. MSG_ENABLEDS -> acquireImage(), MSG_XFERREADY (5 -> 6)
2. DAT_EVENT / MSG_PROCESSEVENT
3. DAT_IMAGEINFO / MSG_GET
4. DAT_SETUPMEMXFER / MSG_GET -> {Min, Max, Preferred}
5. App allocates a TW_MEMORY buffer
6. Loop:
   DAT_IMAGEMEMXFER / MSG_GET  (TheMem allocated)
     -> middle strips: TWRC_SUCCESS
     -> last strip:    TWRC_XFERDONE
     state 6 -> 7 on the first call
7. DAT_PENDINGXFERS / MSG_ENDXFER (7 -> 5)
8. MSG_DISABLEDS (5 -> 4)
```

Return codes: `TWRC_SUCCESS`, `TWRC_XFERDONE`, `TWRC_CANCEL`, `TWCC_SEQERROR`, `TWCC_BADVALUE`.

### 2.2 `TW_SETUPMEMXFER`

`MinBufSize` / `MaxBufSize` / `Preferred`. Apps usually allocate `Preferred`; the DS must tolerate any value in `[MinBufSize, MaxBufSize]`.

### 2.3 `TW_IMAGEMEMXFER`

`Compression`, `BytesPerRow`, `Columns`, `Rows`, `XOffset`, `YOffset`, `BytesWritten`, `Memory{Flags, Length, TheMem}`. `Memory.TheMem` is the app-allocated destination.

### 2.4 Row and byte order

- **Row order**: TWAIN Memory mode is **top-down** (opposite of Windows DIB). First strip = topmost rows; `YOffset` grows from 0.
- **Byte order**: `TWPT_RGB` requires R-G-B in Memory mode; FreeImage / Windows DIB uses B-G-R. 24-bit data must be R/B-swapped on the way out.
- **Row alignment**: 4-byte padded, matching DIB convention (`BytesPerRow = ((Columns * BPP) + 31) / 32 * 4`).

### 2.5 Strip slicing

- Each `DAT_IMAGEMEMXFER` call writes only whole rows: `rows = min(app_buf, remaining) / BytesPerRow`.
- App buffer must hold at least one row; otherwise the DS returns `TWRC_FAILURE + TWCC_BADVALUE`.
- A single row is never split across two calls.

### 2.6 Relation to Native Transfer

Native already materializes the whole image into `image_data_` via `transfer()`. Memory Transfer can reuse that buffer and just slice it.

## 3. Design goals

- 100% compatibility with TWAIN 2.x Memory Transfer apps (Twack 32, NAPS2, Dynamic Web TWAIN, …).
- Reuse `transfer()` for image materialization; avoid duplicating the pixel pipeline.
- Minimal diff: 2 new handlers, 1 new offset member, 1-line capability change; no touch to `VirtualScanner` or settings UI.
- Single output point for the protocol-specific byte order: only the Memory path swaps R/B.

Non-goals: compression (`Compression = TWCP_NONE` only), streaming on-demand decoding, > 8-bit-per-sample pixel types, dynamic `Min/Max/Preferred`, exposing strip size to the UI.

## 4. Overall design

```text
TwainDataSource
├── handleDatSetupMemXfer()       // returns 8K / 256K / 64K
├── handleDatImageMemXfer()       // strip slicer
│     ├── (auto-promote 5 -> 6)
│     ├── transfer() on first call -> image_data_
│     ├── slice rows = min(app_buf, remaining) / BytesPerRow
│     ├── memcpy + 24-bit R/B swap
│     └── return TWRC_SUCCESS / TWRC_XFERDONE
└── reused: transfer / getImageInfo / endXfer / resetXfer / DSM mem shim
```

Data flow:

```text
FreeImage DIB (BGR, bottom-up internally)
        │
        ▼ getScanStrip() flips index -> visually top-down
image_data_  (BGR, top-down, 4-byte aligned)
        │
        ├──> Native: copyDibPixelData() flips rows again -> DIB bottom-up (BGR ok)
        └──> Memory: memcpy rows -> in-place R/B swap -> app buffer (RGB, top-down)
```

## 5. Key decisions and rationale

### 5.1 Reuse `transfer()` and slice `image_data_`

Minimal code; predictable behavior; image_info stays accurate. Cost: same memory footprint as Native, which is acceptable since Memory mode usually serves "process strips as they arrive" rather than DS-side memory savings.

### 5.2 Fixed `MinBufSize / Preferred / MaxBufSize`

8 KB / 64 KB / 256 KB. Simple, predictable, can answer even before `image_info_` is ready.

### 5.3 R/B swap only for 24-bit

TWAIN spec requires R-G-B for `TWPT_RGB`; FreeImage gives B-G-R on Windows. 8-bit / 1-bit have no channels to swap. Swap is done per-strip, not on the whole buffer, so amortized cost is small.

### 5.4 Reject when app buffer < one row

Preserves the `Rows * BytesPerRow == BytesWritten` invariant that apps assume. Apps recover by reallocating to `Preferred`.

### 5.5 Reuse the state 5 -> 6 auto-promote shim

Same compatibility shim as Native. Twack and similar test apps occasionally skip `MSG_PROCESSEVENT`.

### 5.6 Single `mem_xfer_offset_` as the only strip state

Minimal state; `YOffset = mem_xfer_offset_ / BytesPerRow`; reset in three centralized places (ctor / endXfer / resetXfer).

### 5.7 No compression

Out of scope for the minimum-diff goal; Memory-mode customers usually want raw pixels for downstream processing.

### 5.8 Byte-order conversion lives at the protocol exit, not in `image_data_`

Single responsibility: protocol-specific conventions stay at the protocol output. Keeps Native / File paths untouched.

## 6. Component changes

### 6.1 `src/capability.cpp`

- Append `TWSX_MEMORY` to `ICAP_XFERMECH` supported values.

### 6.2 `src/twain_data_source.h`

- Declare `handleDatSetupMemXfer` and `handleDatImageMemXfer`.
- Add `TW_UINT32 mem_xfer_offset_` member.

### 6.3 `src/twain_data_source.cpp`

- Initialize `mem_xfer_offset_(0)` in constructor.
- Add dispatches in DG_CONTROL / DG_IMAGE switches.
- Reset `mem_xfer_offset_` in `endXfer` / `resetXfer`.
- Implement the two handlers (see ​§4 / §6.3 in the Chinese section).

### 6.4 Untouched

`VirtualScanner`, `settings_server.cpp`, `ds_entry.cpp`, DSM memory shim.

### 6.5 Build

No new files, no new dependencies. Both 32-bit and 64-bit builds verified.

## 7. Typical flows

### 7.1 Twack 32 Memory Transfer round trip

```text
1. MSG_OPENDS
2. ICAP_XFERMECH / MSG_SET = TWSX_MEMORY
3. MSG_ENABLEDS, ShowUI=FALSE -> MSG_XFERREADY
4. DAT_IMAGEINFO -> 2480x3508, BPP=24
5. DAT_SETUPMEMXFER -> {8K, 256K, 64K}
6. Allocate 64 KB
7. Loop DAT_IMAGEMEMXFER:
   - first call: transfer() materializes image_data_; state 6 -> 7
   - rows = 64 KB / 7440 ≈ 8 rows per strip; R/B swap; advance offset
   - last strip: TWRC_XFERDONE
8. MSG_ENDXFER -> MSG_DISABLEDS
```

### 7.2 Grayscale Memory Transfer

```text
ICAP_PIXELTYPE = TWPT_GRAY
ICAP_XFERMECH = TWSX_MEMORY
DAT_IMAGEMEMXFER: 8-bit, no R/B swap, just memcpy rows.
```

### 7.3 Buffer-too-small recovery

```text
App buffer = 100 bytes, BytesPerRow = 7440
-> TWRC_FAILURE + TWCC_BADVALUE
App reallocates to 64 KB and retries successfully.
```

## 8. Limitations

- Whole image materialized into `image_data_` (no true streaming savings on DS side).
- No compression (`TWCP_NONE` only).
- No high-bit-depth pixel types.
- Fixed `Min/Max/Preferred`, not adaptive to resolution or pixel type.
- `mem_xfer_offset_` is monotonic; no strip resend.
- Auto-promote 5 -> 6 is a compatibility shim, not strictly spec-compliant.
- No progress callback for the settings UI.
- 24-bit R/B swap is scalar in-place; not SIMD-optimized yet.
- `Memory.Flags` is ignored; the buffer is always treated as a plain pointer.

## 9. Next steps

- Validate more apps: NAPS2, Dynamic Web TWAIN, ScandAll PRO, ImageGear.
- Implement compressed strips (`TWCP_GROUP4` for BW, `TWCP_JPEG` for color).
- Add 16 / 48-bit-per-sample support.
- Make `Min/Preferred/Max` adaptive (e.g. `Preferred = 4 * BytesPerRow`).
- Add a strip-copy progress callback for the settings UI.
- Introduce a true streaming pipeline so `image_data_` is not held entirely in memory.
- SIMD-optimize the 24-bit R/B swap; or emit RGB directly from `transfer()` when the active mech is Memory.
- Expose "default transfer mode" in the settings UI for easier manual testing.
- Add automated tests: stub DSM + bit-exact comparison between Native and Memory outputs.
- Investigate additional memory request modes (e.g. `TWMR_INVERT`, `TWMR_DUAL`); current implementation supports single-buffer loop only.

</details>
