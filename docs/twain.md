# TWAIN 扫描协议介绍 / TWAIN Scanning Protocol Overview

本文档介绍 TWAIN 扫描协议，对比 WIA / eSCL，并梳理 TWAIN 的架构、应用场景和版本演进。

<details open>
<summary>中文说明</summary>

## 1. TWAIN 是什么

**TWAIN** 是一套面向扫描仪、数码相机等图像采集设备的开放工业标准 API，1992 年由 Hewlett-Packard、Kodak、Logitech、Aldus、Caere 等公司联合推出，由 **TWAIN Working Group** (https://www.twain.org) 维护。

名字由来：常被戏称为 "Technology Without An Interesting Name"，官方认为它取自吉卜林的诗句 "And never the twain shall meet"，象征"应用与设备相遇"。

TWAIN 解决的核心问题：

- 让一个图像采集应用（Photoshop、XnView、Office、PDF 软件等）能与各种品牌、型号、接口的扫描仪 / 相机通信，而无需各自写设备驱动。
- 为厂商提供统一的 SDK 入口：硬件厂商写一个符合 TWAIN 的 **Data Source (DS)** 即可被所有 TWAIN 应用使用。

TWAIN 不是底层硬件协议（不像 USB / SCSI），它是应用层 API，运行在操作系统之上，下面再通过厂商 DS 调用真正的设备驱动。

## 2. 与 WIA / eSCL 的比较

### 2.1 三种协议简介

| 协议 | 全称 | 维护方 | 平台 | 传输 |
|---|---|---|---|---|
| TWAIN | TWAIN Protocol | TWAIN Working Group | Windows / macOS / Linux | 进程内 DLL (DS) + DSM 调度 |
| WIA   | Windows Image Acquisition | Microsoft | Windows 专属 | COM 接口 + 内核 stillimg.sys |
| eSCL  | (Mopria / Apple) AirScan / eSCL | Mopria Alliance / Apple | OS 无关 | HTTP + XML over IPP-style URL |

### 2.2 设计哲学对比

| 维度 | TWAIN | WIA | eSCL |
|---|---|---|---|
| 通信形态 | 同进程 DLL 加载 (DS_Entry) | 进程外 COM + 内核驱动 | 网络 HTTP/REST + XML |
| 用户界面 | DS 自带 UI 弹窗（可选） | 系统统一向导（应用可定制） | 应用自带，没有"驱动 UI" |
| 控制粒度 | 极细：上百个 Capability | 中等：限定属性集 | 中等偏粗：纸张 / 颜色 / DPI 等 |
| 厂商负担 | 重：写完整 DS DLL | 中：实现 WIA 驱动接口 | 轻：在固件实现 HTTP 端点 |
| 应用负担 | 中：状态机 + DSM 调用 | 中：COM 方法调用 | 轻：标准 HTTP GET/POST |
| 跨平台 | ✅ Windows + macOS（Linux 部分） | ❌ 仅 Windows | ✅ 任何能 HTTP 的平台 |
| 网络扫描 | 需 TWAIN Direct 扩展 | 通过 WSD 间接支持 | 原生 |
| 32/64 位互操作 | 历史痛点，需 32/64 位 DS 并存 | 无问题（驱动级） | 与位宽无关 |
| Vendor UI 体验 | 体验差异大、可定制 | 统一 Windows 风格 | 应用决定 |
| 高级能力 | 强（barcode、patch、ICC） | 弱 | 中（持续扩展中） |

### 2.3 取舍场景

- 需要细粒度控制扫描参数（双面、ADF、纸张大小、亮度、阈值、压缩选项）、面对专业 / 商用扫描仪 → **TWAIN**。
- 只在 Windows 上、轻量场景（Outlook 插入图片、Microsoft Office 扫描到文档）→ **WIA** 更省事。
- 现代 / 移动 / 跨平台、扫描仪在网络上（家用打印一体机、办公多功能机）→ **eSCL** 已是事实标准（iOS、Android、macOS 默认走它）。
- 多协议混合（同一应用支持桌面老扫描仪 + 网络多功能机）→ 应用层封一层，TWAIN + eSCL 并存。

### 2.4 互补关系

- TWAIN Working Group 推出 **TWAIN Direct**（基于 HTTP/JSON），目的就是把 TWAIN 的语义搬到网络上，与 eSCL 在网络扫描领域形成竞争。
- WIA 在 Windows 7+ 通过 WSD 间接支持网络扫描，但接口陈旧、UX 一般。
- 商用文档扫描仪厂商（Fujitsu、Kodak、Canon DR、Brother、Epson WorkForce）几乎全部提供 TWAIN DS，许多还另带 WIA、ISIS、eSCL 通道。
- 家用 / 办公多功能机（HP、Brother、Canon、Epson）逐步以 eSCL 为主，TWAIN/WIA 走 USB 时仍提供。

## 3. TWAIN 架构

### 3.1 三层架构

```
┌──────────────────────────────┐
│       Application (TWAIN     │   Photoshop, XnView, NAPS2,
│         compliant app)       │   Acrobat, Office, custom EXE
└──────────────┬───────────────┘
               │ DSM_Entry()  (TW_IDENTITY app, TW_IDENTITY ds,
               │                DG, DAT, MSG, pData)
┌──────────────▼───────────────┐
│   Data Source Manager (DSM)  │   TWAINDSM.dll  (TWAIN 2.x)
│                              │   TWAIN_32.dll  (TWAIN 1.x, 32-bit)
└──────────────┬───────────────┘
               │ DS_Entry()   (same signature, ds DLL exports it)
┌──────────────▼───────────────┐
│      Data Source (DS, .ds)   │   Per-device DLL written by vendor.
│      Talks to actual driver  │   Encapsulates USB/network specifics.
└──────────────────────────────┘
```

- **Application**：发起扫描请求、显示扫描结果。引用 DSM 的导入库，调 `DSM_Entry`。
- **Data Source Manager (DSM)**：操作系统级单例 DLL。负责发现、加载、卸载 DS；维护应用与 DS 的会话。
- **Data Source (DS)**：每个设备一个，文件后缀 `.ds`（实际是 DLL）。导出 `DS_Entry`，由 DSM 调用。

### 3.2 DSM 与 DS 的接口

只暴露一个函数签名：

```c
TW_UINT16 DSM_Entry(pTW_IDENTITY origin,
                    pTW_IDENTITY dest,
                    TW_UINT32   DG,
                    TW_UINT16   DAT,
                    TW_UINT16   MSG,
                    TW_MEMREF   pData);
```

`DG / DAT / MSG` 三元组定义所有操作：

- **DG**（Data Group）：CONTROL / IMAGE / AUDIO 等大类。
- **DAT**（Data Argument Type）：参数类型，比如 `DAT_IDENTITY`、`DAT_CAPABILITY`、`DAT_IMAGEINFO`、`DAT_IMAGENATIVEXFER`、`DAT_IMAGEFILEXFER`。
- **MSG**：动作，比如 `MSG_OPENDS`、`MSG_ENABLEDS`、`MSG_GET`、`MSG_SET`、`MSG_RESET`、`MSG_PROCESSEVENT`。

例如打开设备：

```c
DSM_Entry(app_id, NULL,        DG_CONTROL, DAT_IDENTITY,    MSG_OPENDSM, &dsm_window);
DSM_Entry(app_id, NULL,        DG_CONTROL, DAT_IDENTITY,    MSG_GETDEFAULT, &ds_id);
DSM_Entry(app_id, &ds_id,      DG_CONTROL, DAT_IDENTITY,    MSG_OPENDS,  &ds_id);
DSM_Entry(app_id, &ds_id,      DG_CONTROL, DAT_USERINTERFACE, MSG_ENABLEDS, &ui);
```

### 3.3 状态机（7 个状态）

TWAIN 协议显式定义 7 个状态，所有合法操作必须按状态推进：

| 状态 | 名称 | 含义 |
|---|---|---|
| 1 | Pre-Session | 应用还没调用任何 TWAIN API |
| 2 | Source Manager Loaded | 已加载 DSM DLL（旧式手动 LoadLibrary）|
| 3 | Source Manager Open | DSM 已 OPENDSM |
| 4 | Source Open | DS 已 OPENDS，可读写 capabilities |
| 5 | Source Enabled | DS 已 ENABLEDS，UI 可见（如启用），等待用户 / 自动按下 Scan |
| 6 | Transfer Ready | DS 已通知应用有一帧待取 (`MSG_XFERREADY`) |
| 7 | Transferring | 应用正在传输该帧 |

状态推进示意：

```
1 → 2 LoadDSM → 3 MSG_OPENDSM → 4 MSG_OPENDS → 5 MSG_ENABLEDS
                                                  │
                                                  ▼ (DS posts MSG_XFERREADY)
                                                  6 Transfer Ready
                                                  │ DAT_IMAGEINFO
                                                  │ DAT_IMAGENATIVEXFER / DAT_IMAGEFILEXFER
                                                  ▼
                                                  7 Transferring
                                                  │ DAT_PENDINGXFERS (count, end)
                                                  ▼
                                                  5 (next image) or
                                                  4 (DisableDS)
                                                  → 3 (CloseDS) → 2 (CloseDSM) → 1
```

### 3.4 Capability 协商

TWAIN 用一组 **Capability** 描述设备能力，常见前缀：

- `CAP_*`：通用能力（如 `CAP_FEEDERENABLED`、`CAP_UICONTROLLABLE`、`CAP_XFERCOUNT`）。
- `ICAP_*`：图像能力（如 `ICAP_PIXELTYPE`、`ICAP_XRESOLUTION`、`ICAP_UNITS`、`ICAP_IMAGEFILEFORMAT`、`ICAP_XFERMECH`）。
- `ACAP_*`：音频能力。

每个能力以 4 种 **Container** 表达：

- `TWON_ONEVALUE`：单值。
- `TWON_ENUMERATION`：枚举集合 + 默认值 + 当前值。
- `TWON_RANGE`：min/max/step/默认/当前。
- `TWON_ARRAY`：数组（如 `CAP_SUPPORTEDCAPS` 列出所有支持能力）。

每个能力支持以下操作（视实现而定）：`MSG_GET / GETCURRENT / GETDEFAULT / SET / RESET / QUERYSUPPORT`。

### 3.5 数据传输三种机制

`ICAP_XFERMECH` 选择应用如何取像素：

- **Native Transfer** (`TWSX_NATIVE`)：DS 返回 DIB / 内存图像，通过 DSM 共享内存交付。最常用、跨语言绑定友好。
- **File Transfer** (`TWSX_FILE`)：DS 直接把图像保存到应用指定路径，应用读文件。适合自动化、PDF 生成。
- **Memory Transfer** (`TWSX_MEMORY`)：分块（strip / tile）通过内存缓冲区交付，应用控制每次取走的字节数。适合超大图像。

### 3.6 事件循环（Windows）

Windows TWAIN 应用必须把消息循环里收到的 `MSG` 转发给 DS：

```c
MSG msg;
while (GetMessage(&msg, NULL, 0, 0)) {
  TW_EVENT te = { &msg, MSG_NULL };
  DSM_Entry(app_id, &ds_id, DG_CONTROL, DAT_EVENT, MSG_PROCESSEVENT, &te);
  if (te.TWMessage == MSG_XFERREADY) {
    // Begin transfer.
  } else if (te.TWMessage == MSG_CLOSEDSREQ) {
    // User closed the DS UI.
  } else if (te.TWMessage == MSG_NULL) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
}
```

DS 用 `PostMessage` 把异步通知（`MSG_XFERREADY` / `MSG_CLOSEDSREQ` / `MSG_DEVICEEVENT`）塞回应用消息队列，由应用回流给 DS。

### 3.7 32 位 / 64 位

历史上 TWAIN DS 只支持 32 位（`TWAIN_32.dll` + `C:\Windows\twain_32`），所以 64 位应用要么内嵌 32 位 broker 进程，要么不支持 TWAIN。TWAIN 2.x 引入 64 位 DSM (`TWAINDSM.dll`) 和 `C:\Windows\twain_64\`。本项目同时编译 32 位 / 64 位 DS 解决兼容（见 README "Dual architecture"）。

## 4. 应用场景

- **办公文档管理**：PDF/A 归档、票据扫描、合同电子化（Adobe Acrobat、ABBYY、Foxit、Kofax、NAPS2）。
- **专业影像 / 印刷**：高分辨率胶片 / 反射稿扫描（Silverfast、VueScan、Photoshop）。
- **医疗影像**：胶片数字化、病理切片预扫（PACS 前端）。
- **金融票据**：支票扫描（Fujitsu fi 系列、Canon DR 系列、Kodak i 系列广泛使用 TWAIN DS）。
- **OCR / RPA**：OCR 软件（OmniPage、Tesseract 前端）、RPA 平台（UiPath / Blue Prism）批量扫描入库。
- **行业应用**：律所证据扫描、政府档案数字化、医院病历归档。
- **测试 / 开发**：本项目 BN Tech Virtual Scanner 就是为了在没有真扫描仪的开发机上提供一个完整 TWAIN DS，便于扫描应用、PDF 软件、RPA 自动化的回归测试。

## 5. 版本演进

### 5.1 TWAIN 1.x（1992–2005）

- 1992：TWAIN 1.0，首个公开标准。
- 1997：TWAIN 1.7，加入更多 Capability、Mac 支持。
- 仅 32 位（Win32 / Mac OS Classic / macOS PowerPC）。
- DSM 二进制：`TWAIN_32.dll`。
- 字符串使用 8-bit ASCII（`TW_STR32` 等）。
- 状态机已经成型，但能力数量、文件格式较少。

### 5.2 TWAIN 2.x（2008–至今）

- 2008：TWAIN 2.0，引入新 DSM `TWAINDSM.dll`（独立项目，开源 LGPL）。
- 64-bit 原生支持，统一跨平台 DSM。
- Unicode 字符串可选（`TWTY_UNI512` 等）。
- 引入新 Capability：`ICAP_AUTODISCARDBLANKPAGES`、`ICAP_AUTOMATICDESKEW`、`ICAP_BARCODEDETECTIONENABLED` 等。
- 增加 ICC profile、JPEG2000、PDF/A 等文件格式。
- 2015：TWAIN 2.3，加入 metric 单位 / 强化 ADF capabilities。
- 2017：TWAIN 2.4，bugfix + 兼容 macOS sandbox。
- 2022：TWAIN 2.5，本项目目标版本；进一步规范 capability 协商、增强 Native / File transfer 描述。

### 5.3 TWAIN Direct（2017+）

- 不是替代 TWAIN 2.x，而是把 TWAIN 语义搬到网络上。
- 协议：HTTPS + JSON，类似 REST。
- 设备通过 mDNS 发布服务。
- 与 eSCL 在网络扫描领域竞争；目前 eSCL 生态更强。
- 应用形态：浏览器、移动 App 都能用，无需安装 DS。

### 5.4 版本对比

| 维度 | TWAIN 1.x | TWAIN 2.x | TWAIN Direct |
|---|---|---|---|
| 推出年份 | 1992 起 | 2008 起 | 2017 起 |
| DSM | `TWAIN_32.dll` | `TWAINDSM.dll` (含 32/64) | 设备内嵌 HTTPS server |
| 位宽 | 仅 32-bit | 32-bit + 64-bit | 与位宽无关 |
| 字符串 | ASCII | ASCII + 可选 Unicode | UTF-8 (JSON) |
| 传输 | 进程内 DLL | 进程内 DLL | 网络 HTTPS / JSON |
| Vendor UI | DS 自带 | DS 自带或应用 | 应用 |
| 跨平台 | Win + Mac Classic | Win + macOS + Linux 部分 | 任意 HTTP 平台 |
| 网络扫描 | 不直接支持 | 不直接支持 | 原生 |
| 设备发现 | DSM 扫目录 | DSM 扫目录 | mDNS / Bonjour |
| 主要部署 | 老办公场景 | 当前主流商用 | 推广中 |

## 6. 小结

- TWAIN 是历史最悠久、覆盖商用扫描仪最全的扫描标准，能力协商极度细致。
- WIA 是 Windows 局限的轻量替代，适合简单场景但功能受限。
- eSCL 是网络 / 跨平台扫描的事实新标准，正在挤占家用 / 办公多功能机的接口位。
- 本项目实现的是 TWAIN 2.5 兼容的虚拟 Data Source，用于在没有真实硬件的情况下完整模拟 TWAIN 协议链路；详细模块设计见同目录其他设计文档（`docs/native_transfer_design.md`、`docs/file_transfer_design.md`、`docs/pixel_type_design.md` 等）。

</details>

<details>
<summary>English</summary>

## 1. What is TWAIN

**TWAIN** is an open industry standard API for image-acquisition devices (scanners, digital cameras). Introduced in 1992 by Hewlett-Packard, Kodak, Logitech, Aldus, and Caere, it is maintained by the **TWAIN Working Group** (https://www.twain.org).

The name is jokingly read as "Technology Without An Interesting Name"; officially it is said to derive from Kipling's "And never the twain shall meet" — symbolizing "application and device meeting".

TWAIN solves two problems:

- Image-acquisition applications (Photoshop, XnView, Office, PDF tools) can talk to scanners and cameras of any brand / model / bus without writing per-device code.
- Hardware vendors ship one **Data Source (DS)** DLL and it works in every TWAIN-compliant application.

TWAIN is an application-layer API, not a bus protocol; it sits on top of the OS and delegates the actual device I/O to vendor drivers.

## 2. Comparison with WIA and eSCL

### 2.1 Snapshot

| Protocol | Long name | Steward | Platform | Transport |
|---|---|---|---|---|
| TWAIN | TWAIN Protocol | TWAIN Working Group | Windows / macOS / Linux | In-process DLL (DS) + DSM dispatch |
| WIA   | Windows Image Acquisition | Microsoft | Windows-only | COM + kernel stillimg.sys |
| eSCL  | (Mopria / Apple) AirScan / eSCL | Mopria Alliance / Apple | OS-agnostic | HTTP + XML over IPP-style URLs |

### 2.2 Design comparison

| Aspect | TWAIN | WIA | eSCL |
|---|---|---|---|
| Communication | In-process DLL (DS_Entry) | Out-of-process COM + driver | Network HTTP / REST + XML |
| UI | DS may show its own dialog | System wizard | App-owned, no driver UI |
| Control granularity | Very fine (hundreds of capabilities) | Medium | Medium-coarse |
| Vendor burden | Heavy (write full DS DLL) | Medium (WIA driver) | Light (HTTP endpoints in firmware) |
| App burden | Medium (state machine + DSM calls) | Medium (COM calls) | Light (standard HTTP) |
| Cross-platform | Windows + macOS, partial Linux | Windows only | Any HTTP-capable platform |
| Network scan | Needs TWAIN Direct | Indirect via WSD | Native |
| 32/64-bit | Historical pain (need both DS) | Driver-level, no issue | Bit-width irrelevant |
| Vendor UI quality | Varies widely | Uniform Windows style | App decides |
| Advanced features | Strong (barcode, patch, ICC) | Weak | Medium (growing) |

### 2.3 When to pick which

- Need deep control of scan parameters (duplex, ADF, brightness, threshold, compression) and target professional / commercial scanners → **TWAIN**.
- Windows-only and lightweight (Outlook image insert, Office "Scan to document") → **WIA** is simpler.
- Modern / mobile / cross-platform, scanner is on the network (home MFP, office MFP) → **eSCL** is the de-facto modern standard (iOS, Android, macOS use it by default).
- Mixed (legacy USB + modern network) → wrap both in your own abstraction.

### 2.4 Complementary roles

- The TWAIN Working Group released **TWAIN Direct** (HTTPS + JSON) to compete with eSCL on network scanning.
- WIA on Windows 7+ gained indirect network scan via WSD, but the API feels dated.
- Commercial document scanners (Fujitsu, Kodak, Canon DR, Brother, Epson WorkForce) almost universally ship TWAIN DS, often plus WIA / ISIS / eSCL.
- Home / office MFPs (HP, Brother, Canon, Epson) now ship eSCL primarily, with TWAIN / WIA available over USB.

## 3. TWAIN architecture

### 3.1 Three layers

```
┌──────────────────────────────┐
│ Application (TWAIN-aware)    │   Photoshop, XnView, NAPS2, Acrobat, ...
└──────────────┬───────────────┘
               │ DSM_Entry(app, ds, DG, DAT, MSG, pData)
┌──────────────▼───────────────┐
│ Data Source Manager (DSM)    │   TWAINDSM.dll (TWAIN 2.x)
│                              │   TWAIN_32.dll (TWAIN 1.x, 32-bit)
└──────────────┬───────────────┘
               │ DS_Entry(app, ds, DG, DAT, MSG, pData)
┌──────────────▼───────────────┐
│ Data Source (DS, .ds)        │   Per-device DLL written by vendor.
│ Talks to actual hardware     │   Encapsulates USB / network specifics.
└──────────────────────────────┘
```

### 3.2 The DSM / DS entry point

Both DSM and DS expose the same signature:

```c
TW_UINT16 DSM_Entry(pTW_IDENTITY origin,
                    pTW_IDENTITY dest,
                    TW_UINT32   DG,
                    TW_UINT16   DAT,
                    TW_UINT16   MSG,
                    TW_MEMREF   pData);
```

The `DG / DAT / MSG` triple expresses every operation:

- **DG** (Data Group): CONTROL / IMAGE / AUDIO.
- **DAT** (Data Argument Type): payload type, e.g. `DAT_IDENTITY`, `DAT_CAPABILITY`, `DAT_IMAGEINFO`, `DAT_IMAGENATIVEXFER`, `DAT_IMAGEFILEXFER`.
- **MSG**: action, e.g. `MSG_OPENDS`, `MSG_ENABLEDS`, `MSG_GET`, `MSG_SET`, `MSG_PROCESSEVENT`.

Opening a device:

```c
DSM_Entry(app, NULL,    DG_CONTROL, DAT_IDENTITY,      MSG_OPENDSM, &dsm_window);
DSM_Entry(app, NULL,    DG_CONTROL, DAT_IDENTITY,      MSG_GETDEFAULT, &ds_id);
DSM_Entry(app, &ds_id,  DG_CONTROL, DAT_IDENTITY,      MSG_OPENDS,  &ds_id);
DSM_Entry(app, &ds_id,  DG_CONTROL, DAT_USERINTERFACE, MSG_ENABLEDS, &ui);
```

### 3.3 State machine (7 states)

| State | Name | Meaning |
|---|---|---|
| 1 | Pre-Session | Nothing started |
| 2 | Source Manager Loaded | DSM DLL loaded (legacy LoadLibrary) |
| 3 | Source Manager Open | DSM `MSG_OPENDSM` succeeded |
| 4 | Source Open | DS `MSG_OPENDS` succeeded; capabilities readable / writable |
| 5 | Source Enabled | DS `MSG_ENABLEDS`; UI may show; waiting for Scan |
| 6 | Transfer Ready | DS posted `MSG_XFERREADY` |
| 7 | Transferring | App actively pulling an image |

Typical flow:

```
1 → 2 LoadDSM → 3 OPENDSM → 4 OPENDS → 5 ENABLEDS
                                          │
                                          ▼ (DS posts MSG_XFERREADY)
                                          6 Transfer Ready
                                          │ DAT_IMAGEINFO
                                          │ DAT_IMAGENATIVEXFER / DAT_IMAGEFILEXFER
                                          ▼
                                          7 Transferring
                                          │ DAT_PENDINGXFERS
                                          ▼
                                          5 (next image) or
                                          4 (DisableDS)
                                          → 3 (CloseDS) → 2 (CloseDSM) → 1
```

### 3.4 Capability negotiation

TWAIN models device features as **Capabilities**:

- `CAP_*`: general (e.g. `CAP_FEEDERENABLED`, `CAP_UICONTROLLABLE`, `CAP_XFERCOUNT`).
- `ICAP_*`: image (e.g. `ICAP_PIXELTYPE`, `ICAP_XRESOLUTION`, `ICAP_UNITS`, `ICAP_IMAGEFILEFORMAT`, `ICAP_XFERMECH`).
- `ACAP_*`: audio.

Each value is expressed in one of four **containers**:

- `TWON_ONEVALUE`: scalar.
- `TWON_ENUMERATION`: set + default + current.
- `TWON_RANGE`: min / max / step / default / current.
- `TWON_ARRAY`: list (e.g. `CAP_SUPPORTEDCAPS`).

Supported operations: `MSG_GET / GETCURRENT / GETDEFAULT / SET / RESET / QUERYSUPPORT`.

### 3.5 Three transfer mechanisms

`ICAP_XFERMECH` selects how pixels reach the application:

- **Native Transfer** (`TWSX_NATIVE`): DS returns a DIB handle via DSM-managed shared memory. Most common.
- **File Transfer** (`TWSX_FILE`): DS writes the image to an application-specified path; application reads the file. Convenient for automation and PDF generation.
- **Memory Transfer** (`TWSX_MEMORY`): strip-by-strip in memory buffers; application controls each chunk. Good for very large images.

### 3.6 Event loop (Windows)

A TWAIN application must forward window messages to the DS:

```c
MSG msg;
while (GetMessage(&msg, NULL, 0, 0)) {
  TW_EVENT te = { &msg, MSG_NULL };
  DSM_Entry(app, &ds, DG_CONTROL, DAT_EVENT, MSG_PROCESSEVENT, &te);
  if (te.TWMessage == MSG_XFERREADY) {
    // Begin transfer.
  } else if (te.TWMessage == MSG_CLOSEDSREQ) {
    // User closed the DS UI.
  } else if (te.TWMessage == MSG_NULL) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
}
```

The DS uses `PostMessage` to push async notifications (`MSG_XFERREADY`, `MSG_CLOSEDSREQ`, `MSG_DEVICEEVENT`) into the application queue, which the application then forwards back through DSM.

### 3.7 32-bit vs 64-bit

Historically TWAIN DS was 32-bit only (`TWAIN_32.dll` + `C:\Windows\twain_32`), so 64-bit applications needed a 32-bit broker. TWAIN 2.x introduced a 64-bit DSM (`TWAINDSM.dll`) and `C:\Windows\twain_64\`. This project builds both 32-bit and 64-bit DS to cover both worlds (see README "Dual architecture").

## 4. Use cases

- **Office document management**: PDF/A archival, invoices, contracts (Adobe Acrobat, ABBYY, Foxit, Kofax, NAPS2).
- **Professional imaging / prepress**: high-resolution film / reflective scanning (Silverfast, VueScan, Photoshop).
- **Medical imaging**: film digitization, pathology pre-scans (PACS front-ends).
- **Finance**: cheque scanners (Fujitsu fi, Canon DR, Kodak i series).
- **OCR / RPA**: OmniPage, Tesseract front-ends, UiPath, Blue Prism document ingestion.
- **Industry**: legal evidence scanning, government archive digitization, hospital medical records.
- **Test / development**: this project, BN Tech Virtual Scanner, provides a complete TWAIN DS without real hardware for regression testing of scanning applications, PDF tools, and RPA flows.

## 5. Version history

### 5.1 TWAIN 1.x (1992–2005)

- 1992: TWAIN 1.0, first public release.
- 1997: TWAIN 1.7, more capabilities, Mac support.
- 32-bit only (Win32, Mac OS Classic, macOS PowerPC).
- DSM: `TWAIN_32.dll`.
- ASCII strings (`TW_STR32` etc.).
- State machine already defined; smaller capability and format coverage.

### 5.2 TWAIN 2.x (2008–today)

- 2008: TWAIN 2.0, new DSM `TWAINDSM.dll` (separate open-source LGPL project).
- Native 64-bit and unified cross-platform DSM.
- Optional Unicode strings (`TWTY_UNI512`).
- New capabilities: `ICAP_AUTODISCARDBLANKPAGES`, `ICAP_AUTOMATICDESKEW`, `ICAP_BARCODEDETECTIONENABLED`, etc.
- ICC profiles, JPEG 2000, PDF/A.
- 2015: TWAIN 2.3, metric units, stronger ADF support.
- 2017: TWAIN 2.4, bug fixes, macOS sandbox compatibility.
- 2022: TWAIN 2.5, target of this project; tightened capability negotiation and transfer descriptions.

### 5.3 TWAIN Direct (2017+)

- Not a replacement for TWAIN 2.x; brings TWAIN semantics to the network.
- HTTPS + JSON, REST-style.
- Devices advertise themselves via mDNS.
- Competes with eSCL for network scanning; eSCL currently has wider adoption.
- Works from browsers and mobile apps without installing a DS.

### 5.4 Version matrix

| Aspect | TWAIN 1.x | TWAIN 2.x | TWAIN Direct |
|---|---|---|---|
| Year | 1992+ | 2008+ | 2017+ |
| DSM | `TWAIN_32.dll` | `TWAINDSM.dll` (32 + 64) | embedded HTTPS server in device |
| Bit width | 32-bit only | 32 + 64-bit | bit-width irrelevant |
| Strings | ASCII | ASCII + optional Unicode | UTF-8 (JSON) |
| Transport | in-process DLL | in-process DLL | network HTTPS / JSON |
| Vendor UI | DS owns | DS or app | app |
| Cross-platform | Win + Mac Classic | Win + macOS + partial Linux | any HTTP platform |
| Network scan | not direct | not direct | native |
| Discovery | DSM scans directory | DSM scans directory | mDNS / Bonjour |
| Deployment | legacy | current mainstream | growing |

## 6. Summary

- TWAIN is the oldest and most thoroughly featured scanner standard with very fine-grained capability negotiation.
- WIA is a Windows-only lightweight alternative; good for simple scenarios but limited.
- eSCL is the de-facto modern standard for network and cross-platform scanning, increasingly the default for consumer MFPs.
- This project implements a TWAIN 2.5-compatible virtual Data Source for testing applications without real hardware; see the other design docs in this folder (`docs/native_transfer_design.md`, `docs/file_transfer_design.md`, `docs/pixel_type_design.md`, ...) for module-level details.

</details>
