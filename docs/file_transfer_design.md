# File Transfer Mode Design

Design notes for adding TWAIN File Transfer (`TWSX_FILE`) support to BN Tech Virtual Scanner alongside the existing Native Transfer (`TWSX_NATIVE`) path.

<details open>
<summary>中文说明</summary>

## 1. 需求

虚拟扫描仪需要在原有 Native Transfer 之外，再支持 TWAIN File Transfer Mode，使得扫描应用可以选择让 DS 直接把扫描结果写入磁盘文件，再回传文件路径。

主要需求：

- 支持 `ICAP_XFERMECH = TWSX_FILE`。
- 支持 `ICAP_IMAGEFILEFORMAT`，可选 PNG / JPG / BMP / TIFF。
- 支持 `DAT_SETUPFILEXFER` 的 `MSG_GET / MSG_GETDEFAULT / MSG_SET / MSG_RESET`。
- 支持 `DAT_IMAGEFILEXFER` 的 `MSG_GET`，完成实际文件写入并回传 `TW_SETUPFILEXFER`。
- 支持两种文件路径来源：
  - 应用通过 `DAT_SETUPFILEXFER / MSG_SET` 指定的目标路径（XnView "Scan to..." 等应用使用）。
  - settings UI 中用户选择的输出目录 + 文件名 + 格式。
- 当应用已自带文件路径时，settings UI 应隐藏输出相关字段，避免误导用户和覆盖应用路径。
- 文件 DPI 元数据（PNG `pHYs`、JPG JFIF density、BMP `biXPelsPerMeter`、TIFF `XResolution` 等）必须与 settings UI 中选择的 DPI 一致。
- 文件名扩展名必须和所选格式匹配（PNG → `.png`，JPG → `.jpg`，BMP → `.bmp`，TIFF → `.tif`）。
- 不支持的文件格式或路径不存在的目录，DS 不应崩溃，并通过 TWAIN 状态码报告失败。

非功能性需求：

- File Transfer 流程必须遵守 TWAIN 状态机：在 State 6 (`kXferReady`) 进入文件写入，写入成功后转入 State 7 (`kXferring`)，由应用调用 `DAT_PENDINGXFERS / MSG_ENDXFER` 结束。
- 应用可以在 `MSG_XFERREADY` 之后才调用 `DAT_SETUPFILEXFER / MSG_SET`（如 TWACK 这种顺序），DS 必须延迟到 `DAT_IMAGEFILEXFER / MSG_GET` 时才执行真正的文件写入。

## 2. 领域知识

### 2.1 TWAIN 的三种传输机制

TWAIN 标准定义了 `ICAP_XFERMECH` 的三种值：

| 取值 | 含义 |
|---|---|
| `TWSX_NATIVE` | DS 把整幅图作为 DIB 句柄 (`TW_HANDLE`) 一次性返回。 |
| `TWSX_FILE` | DS 把图像写入磁盘文件，回传文件路径和格式。 |
| `TWSX_MEMORY` | DS 按 strip 把图像数据分块返回。本项目暂不支持。 |

本项目当前实现 `TWSX_NATIVE` + `TWSX_FILE`。`CAP_XFERMECH` 默认是 `TWSX_NATIVE`，可由应用或 settings UI 切换为 `TWSX_FILE`。

### 2.2 File Transfer 涉及的 TWAIN triples

File Transfer 模式下，DSM 与 DS 之间通常按以下顺序交互：

```text
1. DG_CONTROL / DAT_CAPABILITY  / MSG_SET    -> ICAP_XFERMECH = TWSX_FILE
2. DG_CONTROL / DAT_CAPABILITY  / MSG_SET    -> ICAP_IMAGEFILEFORMAT = TWFF_PNG
3. DG_CONTROL / DAT_SETUPFILEXFER / MSG_SET  -> 指定文件路径和 Format
4. DG_CONTROL / DAT_USERINTERFACE / MSG_ENABLEDS
5. DG_CONTROL / DAT_EVENT / MSG_PROCESSEVENT -> 等待 MSG_XFERREADY
6. (state 6) 应用可再调用 DAT_SETUPFILEXFER / MSG_SET 更新路径
7. DG_IMAGE / DAT_IMAGEINFO / MSG_GET
8. DG_IMAGE / DAT_IMAGEFILEXFER / MSG_GET    -> DS 写文件，回传路径
9. DG_CONTROL / DAT_PENDINGXFERS / MSG_ENDXFER
10. DG_CONTROL / DAT_USERINTERFACE / MSG_DISABLEDS
```

### 2.3 `TW_SETUPFILEXFER`

`DAT_SETUPFILEXFER` 与 `DAT_IMAGEFILEXFER` 共用 `TW_SETUPFILEXFER` 结构体，主要字段：`FileName`（最长 255 字符路径）、`Format`（`TWFF_PNG / TWFF_JFIF / TWFF_BMP / TWFF_TIFF` ...）、`VRefNum`（Mac 残留字段，本项目固定为 0）。

### 2.4 应用驱动 vs UI 驱动两种路径来源

- 应用驱动（如 XnView 的 "Scan to..."）：应用先弹自家对话框选目录和文件名，再以 `DAT_SETUPFILEXFER / MSG_SET` 把路径传给 DS。DS 不应再让用户在 settings UI 里选输出目录。
- UI 驱动（如 TWAIN 测试工具开启 File 模式但不指定路径）：用户在 DS 的 settings UI 中选择输出目录、文件名和格式，DS 自己生成文件路径。
- 混合（如 TWACK 某些版本）：先 `MSG_ENABLEDS`，settings UI 弹出，关闭后才调用 `DAT_SETUPFILEXFER / MSG_SET`。DS 必须延迟写文件，并避免在 settings UI 中覆盖应用即将提供的路径。

### 2.5 DPI 元数据写入

FreeImage `FreeImage_Save` 在不同格式下对 DPI 字段的处理不一致：

- BMP 一般正确写入 `biXPelsPerMeter` / `biYPelsPerMeter`。
- PNG 通常写入 `pHYs` chunk，但部分版本会忽略。
- JPG 经常不写 JFIF APP0 density 或写错单位。
- TIFF 通常正确写入 `XResolution` / `YResolution` / `ResolutionUnit`。

为了保证 Windows 资源管理器 "属性 → 详细信息" 始终能读到正确的水平/垂直分辨率，本项目在每次保存后都会再用自实现的 patcher 重写一遍 DPI 字段（见 `patchSavedDpiMetadata` 及对应的 `patchPngDpiMetadata` / `patchJpegDpiMetadata` / `patchBmpDpiMetadata` / `patchTiffDpiMetadata`）。

## 3. 设计目标

- 支持完整的 File Transfer 状态机，覆盖 `DAT_SETUPFILEXFER` 全部四种消息和 `DAT_IMAGEFILEXFER / MSG_GET`。
- 支持应用驱动和 UI 驱动两种文件路径来源，并通过 settings UI 的 `app_managed_file_output` 标志区分。
- 与 Native Transfer 共用同一个图像生成管线（`acquireImage` + `preScanPrep`），仅在最后一步选择 DIB 返回还是磁盘写入。
- 文件 DPI 元数据始终与 settings UI 中的 DPI 一致，覆盖 PNG / JPG / BMP / TIFF。
- 设计上为后续扩展更多文件格式（如多页 TIFF、PDF 等）保留入口。

非目标：

- 不支持 `TWSX_MEMORY`。
- 不支持多页文件（即使 TIFF 也只写单页）。
- 不支持加密压缩选项；JPG 质量固定为 85。
- 不支持自定义 PDF / OCR 输出。

## 4. 总体设计

File Transfer 在已有 Native Transfer 模块上以最小侵入的方式叠加：

```text
TwainDataSource
├── handleDatSetupFileXfer()       // 路径协商
├── handleDatImageFileXfer()       // 触发写文件 + 回传路径
└── enableDs()                     // 在 ShowUI=TRUE 时联动 settings UI
        │
        ├── SettingsServer (HTML UI)
        │     └── app_managed_file_output 决定是否显示输出字段
        │
        └── VirtualScanner
              ├── acquireImage() + preScanPrep()
              ├── saveImageToFile()     // UI 提供 dir + filename + format
              ├── saveImageToPath()     // 应用直接提供完整路径
              ├── applyDpiMetadata()    // 写入 FreeImage 内部 DPI
              └── patchSavedDpiMetadata() // 二次修补 PNG/JPG/BMP/TIFF 容器
```

关键流程：

1. 应用设置 `ICAP_XFERMECH = TWSX_FILE`、`ICAP_IMAGEFILEFORMAT = ...`。
2. 应用可选调用 `DAT_SETUPFILEXFER / MSG_SET`，DS 记录到 `app_file_path_`，并根据扩展名校正 `ICAP_IMAGEFILEFORMAT`。
3. `MSG_ENABLEDS`：
   - 若 `ShowUI=TRUE`，弹出 settings UI；
     - 若 `cur_mech == TWSX_FILE`，则 `app_managed_file_output = true`，UI 不显示输出字段；
     - 否则用户可以在 UI 里勾选 File 模式并选输出目录 / 文件名 / 格式。
   - 调用 `acquireImage()` 把图像准备好，但**不写文件**。
   - 发送 `MSG_XFERREADY`。
4. 应用收到 `MSG_XFERREADY` 后，可再次调用 `DAT_SETUPFILEXFER / MSG_SET` 更新路径。
5. 应用调用 `DAT_IMAGEFILEXFER / MSG_GET`：
   - `app_file_path_` 非空 → 调用 `saveImageToPath()`；
   - 否则 → 调用 `saveImageToFile()`，使用 UI 选择的目录 / 文件名 / 格式。
   - 写入成功后回填 `data->FileName / Format / VRefNum`，状态转 `kXferring`，返回 `TWRC_XFERDONE`。
6. 应用 `DAT_PENDINGXFERS / MSG_ENDXFER` 结束，DS 清理 `app_file_path_`。

## 5. 重要决策和原因

### 5.1 延迟到 `DAT_IMAGEFILEXFER / MSG_GET` 才写文件

决策：`enableDs()` 内只调用 `acquireImage()` 完成像素准备，文件写入延迟到 `DAT_IMAGEFILEXFER / MSG_GET`。

原因：

- TWAIN 规范允许应用在 State 6 (`MSG_XFERREADY` 之后) 才调用 `DAT_SETUPFILEXFER / MSG_SET`，TWACK 等工具也确实这么做。
- 如果在 `enableDs()` 就写文件，应用提供的新路径会被忽略，导致文件落在错的位置。
- 延迟写入只多保留一份内存 DIB，对内存影响可控。

### 5.2 同时支持 `saveImageToFile()` 与 `saveImageToPath()`

决策：在 `VirtualScanner` 中提供两条保存接口：

- `saveImageToFile()`：使用 `output_dir_` + `output_filename_` + `output_format_`（来自 settings UI）。
- `saveImageToPath(path)`：直接保存到指定绝对/相对路径，格式从扩展名推断。

原因：

- 应用驱动场景（XnView "Scan to..."）必须严格落到应用指定的路径，DS 不能改名、不能改目录。
- UI 驱动场景需要 DS 自己生成时间戳文件名，并写入用户选择的目录。
- 两条路径都共享 `applyDpiMetadata` + `patchSavedDpiMetadata`，保证 DPI 元数据一致。

### 5.3 settings UI 引入 `app_managed_file_output` 标志

决策：在 `enableDs()` 中判断当前 `ICAP_XFERMECH`，若已是 `TWSX_FILE`，则把 `ui_result.app_managed_file_output` 设为 `true`，settings UI 隐藏输出目录、文件名、格式等控件。

原因：

- 在应用驱动场景下，输出位置完全由应用决定，UI 字段会误导用户，让用户以为自己改了目录有效。
- 隐藏字段后用户只能改颜色模式、分辨率、纸张大小等 DS 内部设置。
- 避免 settings UI 在合并 `ScannerSettings` 时覆盖应用即将提供的 `app_file_path_`。

### 5.4 扩展名推断 + `ICAP_IMAGEFILEFORMAT` 自动校正

决策：`DAT_SETUPFILEXFER / MSG_SET` 收到路径后，会根据扩展名（`.png` / `.jpg|.jpeg` / `.bmp` / `.tif|.tiff`）推断格式，并调用 `caps_.setCurrentValue(ICAP_IMAGEFILEFORMAT, ff)`。

原因：

- 部分应用不会在 `MSG_SET` 时提供合法 `Format`，只填路径；缺省值经常是 `0` 或 `TWFF_PNG`。
- 写出的扩展名必须与文件格式匹配，否则 Explorer 和图像应用都无法识别。
- 自动校正 `ICAP_IMAGEFILEFORMAT` 使后续 `MSG_GET` 返回的 `Format` 与实际写入的文件一致。

### 5.5 文件 DPI 元数据二次修补

决策：保存后再用本项目的 `patchPngDpiMetadata / patchJpegDpiMetadata / patchBmpDpiMetadata / patchTiffDpiMetadata` 修补一次。

原因：

- FreeImage 在不同版本/不同格式下对 DPI 元数据的写入不稳定，特别是 PNG `pHYs` 和 JPG JFIF density。
- 修补后能保证 Windows 资源管理器 "属性 → 详细信息" 中的水平/垂直分辨率显示正确。
- 集中放在 `patchSavedDpiMetadata` 内调度，新增格式只需扩展这一处。

### 5.6 `MSG_RESET` 清空 `app_file_path_`

决策：`DAT_SETUPFILEXFER / MSG_RESET` 清空 `app_file_path_`，并 fall through 到 `MSG_GET` 返回当前（空）路径和当前 `ICAP_IMAGEFILEFORMAT`。

原因：

- 符合 TWAIN MSG_RESET 语义：把当前值复位为默认值，本项目默认为 "无应用提供路径"，回退到 UI 驱动。
- `closeDs()` 也会清空 `app_file_path_`，避免跨会话残留。

## 6. 架构各组件改动点

### 6.1 `capability.cpp`

- `ICAP_XFERMECH` 增加 `TWSX_FILE` 到可选值列表。
- 新增 `ICAP_IMAGEFILEFORMAT`，默认 `TWFF_PNG`，可选 `TWFF_TIFF / TWFF_BMP / TWFF_JFIF / TWFF_PNG`。
- 与 `ICAP_XFERMECH` 一并暴露给 `CAP_SUPPORTEDCAPS`。

### 6.2 `twain_data_source.h / .cpp`

- 新增成员 `std::string app_file_path_`，记录应用通过 `DAT_SETUPFILEXFER / MSG_SET` 提供的路径。
- 新增 dispatch 入口：
  - `DAT_SETUPFILEXFER → handleDatSetupFileXfer()`
  - `DAT_IMAGEFILEXFER → handleDatImageFileXfer()`
- `handleDatSetupFileXfer`：
  - `MSG_SET`：记录路径、按扩展名校正 `ICAP_IMAGEFILEFORMAT`。
  - `MSG_GET / MSG_GETDEFAULT`：回传 `app_file_path_` 与当前 `ICAP_IMAGEFILEFORMAT`。
  - `MSG_RESET`：清空 `app_file_path_`，然后 fall-through 到 `MSG_GET`。
- `handleDatImageFileXfer`：
  - 必须在 `kXferReady` 状态。
  - 根据 `app_file_path_` 决定 `saveImageToPath()` 还是 `saveImageToFile()`。
  - 回填 `data->FileName / Format / VRefNum`，状态切到 `kXferring`，返回 `TWRC_XFERDONE`。
- `enableDs()`：
  - 当 `ShowUI=TRUE` 且 `ICAP_XFERMECH == TWSX_FILE` 时，置 `ui_result.app_managed_file_output = true`，隐藏 UI 输出字段。
  - settings UI 结果回写到 `ICAP_XFERMECH / ICAP_IMAGEFILEFORMAT` 与 `VirtualScanner` 的 `output_dir_ / output_format_ / output_filename_`。
  - 不论 Native 还是 File 模式，都先 `acquireImage()`，再发 `MSG_XFERREADY`。
- `closeDs()`：清理 `app_file_path_`，避免跨会话残留。

### 6.3 `virtual_scanner.h / .cpp`

- 新增成员：`output_dir_ / output_format_ / output_filename_ / last_saved_file_`。
- 新增方法：
  - `setOutputDir / setOutputFormat / setOutputFilename`
  - `saveImageToFile()`：组合 `output_dir_` + `output_filename_` + 格式扩展名。空文件名自动生成 `scan_YYYYMMDD_HHMMSS` 时间戳。`SHCreateDirectoryExA` 确保目录存在。
  - `saveImageToPath(path)`：解析相对路径，按扩展名匹配 `FREE_IMAGE_FORMAT`，确保父目录存在。
  - `getLastSavedFilePath()`：供 `handleDatImageFileXfer` 回填 `FileName`。
- 在保存路径上统一调用 `applyDpiMetadata()`（写 FreeImage 内部 DPI + EXIF）和 `patchSavedDpiMetadata()`（PNG/JPG/BMP/TIFF 容器级补写）。
- 支持的 `FREE_IMAGE_FORMAT`：`FIF_PNG / FIF_JPEG / FIF_BMP / FIF_TIFF`，与 `ICAP_IMAGEFILEFORMAT` 的枚举一一对应。

### 6.4 `settings_server.cpp` (HTML UI)

- `SettingsUiResult` 新增字段：
  - `transfer_mode`：0 = Native，1 = File。
  - `file_format`：0/1/2/3 → PNG/JPG/BMP/TIFF。
  - `output_dir / output_filename`：字符数组。
  - `app_managed_file_output`：bool，应用是否已经接管输出路径。
- HTML 在 `app_managed_file_output == true` 时仅显示一行说明文字，隐藏 transfer mode / format / output 字段。
- 在 Native 默认场景下预填一个 `scan_YYYYMMDD_HHMMSS` 文件名，方便用户切到 File 模式时直接扫描。
- 通过 JS 动态控制 Format / Output Dir / Output Filename 行的显示，并把 `transfer_mode = 1` 时的扩展名联动显示。
- 新增 `/browse` 端点调用 `SHBrowseForFolderW`，让用户图形化选择输出目录。

### 6.5 文件 DPI 修补模块

- `patchPngDpiMetadata`：插入或替换 `pHYs` chunk，单位 1（pixels per meter）。
- `patchJpegDpiMetadata`：在 JFIF APP0 中写入 density，单位为 dots per inch。
- `patchBmpDpiMetadata`：直接覆盖 `BITMAPINFOHEADER.biXPelsPerMeter / biYPelsPerMeter`。
- `patchTiffDpiMetadata`：替换 `XResolution / YResolution / ResolutionUnit` 标签。

这些 patcher 既服务于 File Transfer，也服务于 Native Transfer 应用自己保存文件的链路。

## 7. 典型流程示例

### 7.1 XnView "Scan to..."（应用驱动）

```text
1. App: ICAP_XFERMECH = TWSX_FILE
2. App: DAT_SETUPFILEXFER / MSG_SET, FileName="D:\out\page.tif", Format=TWFF_TIFF
3. App: MSG_ENABLEDS, ShowUI=TRUE
   DS:  app_managed_file_output=true -> UI 隐藏输出区
        用户选 600 DPI / RGB -> 点 Scan
   DS:  acquireImage(), MSG_XFERREADY
4. App: DAT_IMAGEINFO / MSG_GET
   DS:  返回 600 DPI 的 TW_IMAGEINFO
5. App: DAT_IMAGEFILEXFER / MSG_GET
   DS:  saveImageToPath("D:\out\page.tif") + patchTiffDpiMetadata
        回传 FileName / Format=TWFF_TIFF / VRefNum=0, TWRC_XFERDONE
6. App: DAT_PENDINGXFERS / MSG_ENDXFER -> MSG_DISABLEDS
```

### 7.2 settings UI 驱动

```text
1. App: MSG_ENABLEDS, ShowUI=TRUE
   DS:  ICAP_XFERMECH 当前为 TWSX_NATIVE -> UI 显示完整输出区
        用户选 File / PNG / D:\scans\ / 默认时间戳文件名 -> Scan
   DS:  setCurrentValue(ICAP_XFERMECH, TWSX_FILE)
        setCurrentValue(ICAP_IMAGEFILEFORMAT, TWFF_PNG)
        scanner_.setOutputDir / setOutputFormat / setOutputFilename
        acquireImage(), MSG_XFERREADY
2. App: DAT_IMAGEFILEXFER / MSG_GET
   DS:  app_file_path_ 为空 -> saveImageToFile()
        生成 D:\scans\scan_20260526_153012.png
        patchPngDpiMetadata -> 回传路径 + Format=TWFF_PNG
3. App: MSG_ENDXFER -> MSG_DISABLEDS
```

## 8. 限制

- 不支持 `TWSX_MEMORY` strip 模式；某些只识别 Memory 模式的旧应用会无法工作。
- 不支持多页文件输出（多页 TIFF / PDF）。`pending_xfers_.Count` 固定为 1，应用每次只能拿到一张图。
- JPG 压缩质量写死为 85，没有 `ICAP_IMAGEFILEXFER` 相关的质量协商。
- `ICAP_IMAGEFILEFORMAT` 与 `output_format_`（0=PNG, 1=JPG, 2=BMP, 3=TIFF）的映射写在两个文件里（`twain_data_source.cpp` 与 `virtual_scanner.cpp`），如果未来增加格式需要同步两处。
- `saveImageToPath` 仅按扩展名识别格式：扩展名缺失或拼写错误时一律回落 PNG，与应用期望可能不一致。
- 没有 `TW_SETUPFILEXFER2`，因此最长路径仍受 `TW_STR255` 的 255 字符限制，超出会被截断。
- File Transfer 写文件是同步阻塞的，大图 + 慢盘场景下会阻塞 TWAIN 消息线程；应用层可能观察到短暂无响应。
- 写文件失败时只返回 `TWCC_BUMMER`，没有更细粒度的错误码（磁盘满、权限不足、目录不存在等无法区分）。
- settings UI 与 `app_managed_file_output` 的判断依赖于 `enableDs()` 调用时刻的 `ICAP_XFERMECH`，如果应用先 `ENABLEDS` 再 `SET XFERMECH` 会出现 UI 与最终模式不一致（目前未观察到这种顺序，但规范上不禁止）。

## 9. 下一步工作

- 评估并实现 `TWSX_MEMORY` strip 传输，覆盖只支持 Memory 模式的老应用。
- 引入多页 TIFF / PDF 支持，使 `pending_xfers_.Count` 可大于 1，匹配 ADF 模拟场景（如果将来支持 ADF）。
- 暴露 JPG 质量、PNG 压缩等级等设置项，可通过 settings UI 或独立 capability 协商。
- 把文件保存挪到 worker 线程，避免长时间阻塞 TWAIN 消息线程，并在 settings UI 上展示进度。
- 把格式映射（`ICAP_IMAGEFILEFORMAT` <-> `FREE_IMAGE_FORMAT` <-> extension <-> UI index）集中到一个表里，消除三处重复。
- 给 `saveImageToPath` 增加 fallback：扩展名识别失败时改用 `ICAP_IMAGEFILEFORMAT` 当前值，而不是固定 PNG。
- 增加更细的错误码：磁盘满 -> `TWCC_LOWMEMORY` / 自定义，权限拒绝 -> `TWCC_DENIED`（如可用），目录不存在并创建失败 -> 单独提示。
- 增加 `TW_SETUPFILEXFER2` 支持，突破 255 字符的路径长度限制（需要长路径感知）。
- 在 TWACK / XnView / NAPS2 / Twack2 等多个应用上做集成测试，记录每个应用对 `DAT_SETUPFILEXFER` / `DAT_IMAGEFILEXFER` 的调用顺序。
- 给 settings UI 增加 "File Transfer 由应用接管" 的更明显视觉提示（图标 + 当前路径预览）。

</details>

<details>
<summary>English</summary>

## 1. Requirement

In addition to the existing Native Transfer path, the virtual scanner needs to support TWAIN File Transfer mode, so an application can let the data source write the scanned image to disk and just receive the resulting file path back.

Main requirements:

- Support `ICAP_XFERMECH = TWSX_FILE`.
- Support `ICAP_IMAGEFILEFORMAT` with PNG / JPG / BMP / TIFF.
- Support `DAT_SETUPFILEXFER` with `MSG_GET / MSG_GETDEFAULT / MSG_SET / MSG_RESET`.
- Support `DAT_IMAGEFILEXFER / MSG_GET` performing the actual file write and returning a populated `TW_SETUPFILEXFER`.
- Support two file path sources:
  - Application-supplied path via `DAT_SETUPFILEXFER / MSG_SET` (XnView "Scan to..." etc.).
  - User-selected output directory + filename + format from the settings UI.
- Hide UI output fields when the application has already taken over file output, to avoid misleading the user and accidentally overwriting the app-supplied path.
- File DPI metadata (PNG `pHYs`, JPG JFIF density, BMP `biXPelsPerMeter`, TIFF `XResolution`, ...) must match the DPI selected in the settings UI.
- File extension must match the chosen format (PNG -> `.png`, JPG -> `.jpg`, BMP -> `.bmp`, TIFF -> `.tif`).
- Unsupported formats or missing directories must not crash the DS; failure should be reported with a TWAIN condition code.

Non-functional requirements:

- The flow must follow the TWAIN state machine: enter file write at State 6 (`kXferReady`), transition to State 7 (`kXferring`) on success, and end via `DAT_PENDINGXFERS / MSG_ENDXFER`.
- Applications may call `DAT_SETUPFILEXFER / MSG_SET` after `MSG_XFERREADY` (e.g. TWACK), so the DS must defer the actual file write until `DAT_IMAGEFILEXFER / MSG_GET`.

## 2. Domain knowledge

### 2.1 The three TWAIN transfer mechanisms

TWAIN defines three values for `ICAP_XFERMECH`:

| Value | Meaning |
|---|---|
| `TWSX_NATIVE` | DS returns the whole image as a DIB handle. |
| `TWSX_FILE` | DS writes the image to disk and returns the file path and format. |
| `TWSX_MEMORY` | DS returns the image in strips. Not supported in this project. |

This project supports `TWSX_NATIVE` and `TWSX_FILE`. `CAP_XFERMECH` defaults to `TWSX_NATIVE` and can be switched to `TWSX_FILE` either by the application or in the settings UI.

### 2.2 TWAIN triples involved in File Transfer

```text
1. DG_CONTROL / DAT_CAPABILITY  / MSG_SET   -> ICAP_XFERMECH = TWSX_FILE
2. DG_CONTROL / DAT_CAPABILITY  / MSG_SET   -> ICAP_IMAGEFILEFORMAT = TWFF_PNG
3. DG_CONTROL / DAT_SETUPFILEXFER / MSG_SET -> Optional path + Format
4. DG_CONTROL / DAT_USERINTERFACE / MSG_ENABLEDS
5. DG_CONTROL / DAT_EVENT / MSG_PROCESSEVENT -> wait MSG_XFERREADY
6. (state 6) Optionally another DAT_SETUPFILEXFER / MSG_SET to update path
7. DG_IMAGE / DAT_IMAGEINFO / MSG_GET
8. DG_IMAGE / DAT_IMAGEFILEXFER / MSG_GET   -> DS writes file, returns path
9. DG_CONTROL / DAT_PENDINGXFERS / MSG_ENDXFER
10. DG_CONTROL / DAT_USERINTERFACE / MSG_DISABLEDS
```

### 2.3 `TW_SETUPFILEXFER`

`DAT_SETUPFILEXFER` and `DAT_IMAGEFILEXFER` share `TW_SETUPFILEXFER`: `FileName` (max 255 chars), `Format` (`TWFF_*`), and the Mac-legacy `VRefNum` (always 0 here).

### 2.4 Application-driven vs UI-driven paths

- App-driven (XnView "Scan to..."): the app pops up its own dialog and supplies the destination path via `DAT_SETUPFILEXFER / MSG_SET`. The DS must not expose its own output directory fields in this case.
- UI-driven: the user picks directory, filename, and format in the settings UI; the DS builds the path itself.
- Mixed (TWACK): the app first calls `MSG_ENABLEDS`, then sends `DAT_SETUPFILEXFER / MSG_SET` after the settings UI closes. The DS must defer the file write and must not overwrite the path the app is about to supply.

### 2.5 DPI metadata

`FreeImage_Save` is inconsistent about DPI metadata across formats and versions. To guarantee correct DPI in Windows Explorer's Details tab and downstream applications, the project re-patches DPI fields after saving via format-specific patchers (`patchPngDpiMetadata`, `patchJpegDpiMetadata`, `patchBmpDpiMetadata`, `patchTiffDpiMetadata`).

## 3. Design goals

- Cover the full File Transfer state machine: all four `DAT_SETUPFILEXFER` messages plus `DAT_IMAGEFILEXFER / MSG_GET`.
- Support both app-driven and UI-driven path sources via the `app_managed_file_output` flag flowing into the settings UI.
- Share the image pipeline (`acquireImage` + `preScanPrep`) with Native Transfer; only the final step differs (DIB vs file).
- Keep file DPI metadata aligned with the UI-selected DPI for PNG / JPG / BMP / TIFF.
- Leave hooks for future formats (multi-page TIFF, PDF, etc.).

Non-goals:

- No `TWSX_MEMORY`.
- No multi-page output.
- No encryption/compression negotiation; JPG quality is fixed at 85.
- No PDF / OCR output.

## 4. Overall design

File Transfer layers minimally on top of Native Transfer:

```text
TwainDataSource
├── handleDatSetupFileXfer()       // Path negotiation
├── handleDatImageFileXfer()       // Triggers file write + returns path
└── enableDs()                     // Drives settings UI when ShowUI=TRUE
        │
        ├── SettingsServer (HTML UI)
        │     └── app_managed_file_output gates output fields
        │
        └── VirtualScanner
              ├── acquireImage() + preScanPrep()
              ├── saveImageToFile()      // Uses UI dir + filename + format
              ├── saveImageToPath()      // Uses app-supplied full path
              ├── applyDpiMetadata()
              └── patchSavedDpiMetadata()
```

High-level flow:

1. App sets `ICAP_XFERMECH = TWSX_FILE` and `ICAP_IMAGEFILEFORMAT`.
2. App optionally calls `DAT_SETUPFILEXFER / MSG_SET`; the DS records it in `app_file_path_` and fixes `ICAP_IMAGEFILEFORMAT` from the extension.
3. `MSG_ENABLEDS`:
   - If `ShowUI=TRUE`, show settings UI. When `cur_mech == TWSX_FILE`, set `app_managed_file_output = true` and hide output fields.
   - Call `acquireImage()` to prepare the pixels but do not write the file.
   - Emit `MSG_XFERREADY`.
4. App may call `DAT_SETUPFILEXFER / MSG_SET` again after `MSG_XFERREADY`.
5. On `DAT_IMAGEFILEXFER / MSG_GET`:
   - Non-empty `app_file_path_` -> `saveImageToPath()`.
   - Otherwise -> `saveImageToFile()` using UI settings.
   - Populate `data->FileName / Format / VRefNum`, move to `kXferring`, return `TWRC_XFERDONE`.
6. App ends the transfer with `DAT_PENDINGXFERS / MSG_ENDXFER`. `closeDs()` clears `app_file_path_`.

## 5. Key decisions and rationale

### 5.1 Defer file write until `DAT_IMAGEFILEXFER / MSG_GET`

`enableDs()` only calls `acquireImage()`. The actual save happens later, because the spec allows `DAT_SETUPFILEXFER / MSG_SET` in State 6, after `MSG_XFERREADY`, and tools like TWACK use this pattern.

### 5.2 Both `saveImageToFile()` and `saveImageToPath()`

App-driven scenarios must write exactly to the path the app supplied. UI-driven scenarios need the DS to invent a timestamped filename under a user-selected directory. Two entry points keep both code paths simple; both share `applyDpiMetadata` + `patchSavedDpiMetadata`.

### 5.3 `app_managed_file_output` flag in the settings UI

When `enableDs()` sees `ICAP_XFERMECH == TWSX_FILE`, it tells the settings UI to hide all output controls. This prevents the user from changing fields that won't be honored (the app owns the path) and avoids accidentally overwriting `app_file_path_`.

### 5.4 Extension-based format fix-up in `DAT_SETUPFILEXFER / MSG_SET`

Some applications send `Format = 0` or always send `TWFF_PNG`. The DS derives the format from the file extension and updates `ICAP_IMAGEFILEFORMAT`, so the written file matches its extension and later `MSG_GET` queries return a consistent format.

### 5.5 Post-save DPI metadata patching

FreeImage's DPI handling is uneven across formats and versions. Re-patching after `FreeImage_Save` guarantees correct horizontal/vertical DPI in Explorer and other consumers.

### 5.6 `MSG_RESET` clears `app_file_path_`

`MSG_RESET` is treated as "no app-supplied path, fall back to UI-driven". `closeDs()` clears it too, so the path does not leak across sessions.

## 6. Component changes

### 6.1 `capability.cpp`

- Add `TWSX_FILE` to `ICAP_XFERMECH` choices.
- Add `ICAP_IMAGEFILEFORMAT` (default `TWFF_PNG`) with `TWFF_PNG / TWFF_JFIF / TWFF_BMP / TWFF_TIFF`.
- Expose both in `CAP_SUPPORTEDCAPS`.

### 6.2 `twain_data_source.h / .cpp`

- New member `std::string app_file_path_`.
- New dispatch:
  - `DAT_SETUPFILEXFER -> handleDatSetupFileXfer()`
  - `DAT_IMAGEFILEXFER -> handleDatImageFileXfer()`
- `handleDatSetupFileXfer`:
  - `MSG_SET`: store the path; auto-correct `ICAP_IMAGEFILEFORMAT` from the extension.
  - `MSG_GET / MSG_GETDEFAULT`: return current `app_file_path_` and `ICAP_IMAGEFILEFORMAT`.
  - `MSG_RESET`: clear `app_file_path_` then fall through to `MSG_GET`.
- `handleDatImageFileXfer`:
  - Requires `kXferReady`.
  - Branches between `saveImageToPath()` and `saveImageToFile()`.
  - Fills `FileName / Format / VRefNum`, transitions to `kXferring`, returns `TWRC_XFERDONE`.
- `enableDs()`:
  - Sets `app_managed_file_output = true` when `ShowUI=TRUE` and `ICAP_XFERMECH == TWSX_FILE`.
  - Writes UI choices back into capabilities and into `VirtualScanner`.
  - Always calls `acquireImage()` before `MSG_XFERREADY`, regardless of transfer mode.
- `closeDs()` resets `app_file_path_`.

### 6.3 `virtual_scanner.h / .cpp`

- Adds `output_dir_ / output_format_ / output_filename_ / last_saved_file_` and setters.
- Adds `saveImageToFile()`, `saveImageToPath(path)`, and `getLastSavedFilePath()`.
- Ensures the destination directory exists via `SHCreateDirectoryExA`.
- Maps `ICAP_IMAGEFILEFORMAT` index -> `FREE_IMAGE_FORMAT` and `.ext`.
- Calls `applyDpiMetadata()` before save and `patchSavedDpiMetadata()` after.

### 6.4 `settings_server.cpp` (HTML UI)

- `SettingsUiResult` adds `transfer_mode / file_format / output_dir / output_filename / app_managed_file_output`.
- The HTML renders the output group only when `app_managed_file_output == false`.
- JS hides/shows format / output rows based on the transfer mode selection.
- A `/browse` endpoint hosts `SHBrowseForFolderW` for picking the output directory.

### 6.5 File DPI patchers

`patchPngDpiMetadata`, `patchJpegDpiMetadata`, `patchBmpDpiMetadata`, and `patchTiffDpiMetadata` rewrite the container-level DPI fields after `FreeImage_Save`. They are reused by File Transfer and by any Native Transfer flow where the application saves the file.

## 7. Typical flows

### 7.1 XnView "Scan to..." (app-driven)

```text
1. App: ICAP_XFERMECH = TWSX_FILE
2. App: DAT_SETUPFILEXFER / MSG_SET, FileName="D:\out\page.tif", Format=TWFF_TIFF
3. App: MSG_ENABLEDS, ShowUI=TRUE
   DS:  app_managed_file_output=true -> UI hides output group
        User picks 600 DPI / RGB -> Scan
   DS:  acquireImage(), MSG_XFERREADY
4. App: DAT_IMAGEINFO / MSG_GET -> DS returns 600 DPI image info
5. App: DAT_IMAGEFILEXFER / MSG_GET
   DS:  saveImageToPath("D:\out\page.tif") + patchTiffDpiMetadata
        returns FileName / Format=TWFF_TIFF, TWRC_XFERDONE
6. App: DAT_PENDINGXFERS / MSG_ENDXFER -> MSG_DISABLEDS
```

### 7.2 UI-driven

```text
1. App: MSG_ENABLEDS, ShowUI=TRUE
   DS:  XFERMECH currently TWSX_NATIVE -> UI shows full output group
        User picks File / PNG / D:\scans\ / default timestamp -> Scan
   DS:  setCurrentValue(ICAP_XFERMECH, TWSX_FILE)
        setCurrentValue(ICAP_IMAGEFILEFORMAT, TWFF_PNG)
        scanner_.setOutputDir / setOutputFormat / setOutputFilename
        acquireImage(), MSG_XFERREADY
2. App: DAT_IMAGEFILEXFER / MSG_GET
   DS:  app_file_path_ empty -> saveImageToFile()
        writes D:\scans\scan_20260526_153012.png
        patchPngDpiMetadata -> returns path + Format=TWFF_PNG
3. App: MSG_ENDXFER -> MSG_DISABLEDS
```

## 8. Limitations

- `TWSX_MEMORY` strip mode is not implemented; legacy apps that only support Memory mode will not work.
- No multi-page output (multi-page TIFF / PDF). `pending_xfers_.Count` is fixed to 1.
- JPG quality is hard-coded at 85; there is no quality-related capability negotiation.
- The `ICAP_IMAGEFILEFORMAT` <-> `output_format_` mapping is duplicated between `twain_data_source.cpp` and `virtual_scanner.cpp`; adding a format requires updating both.
- `saveImageToPath` infers format from extension only; missing or misspelled extensions silently fall back to PNG.
- No `TW_SETUPFILEXFER2`, so paths are capped at 255 characters and silently truncated beyond that.
- File writes are synchronous on the TWAIN thread; large images on slow disks block the message loop briefly.
- On write failure only `TWCC_BUMMER` is returned; disk-full, permission, and missing-directory cases are not distinguished.
- `app_managed_file_output` is decided at `enableDs()` time; an app that switches `ICAP_XFERMECH` only after `MSG_ENABLEDS` would see a UI that does not match the final mode.

## 9. Next steps

- Evaluate and implement `TWSX_MEMORY` strip transfer to cover Memory-only applications.
- Add multi-page TIFF / PDF output and allow `pending_xfers_.Count > 1`, in preparation for a future simulated ADF.
- Expose JPG quality and PNG compression level either through settings UI or via capability negotiation.
- Move file writes off the TWAIN thread and surface progress in the settings UI.
- Consolidate the format mapping (`ICAP_IMAGEFILEFORMAT` <-> `FREE_IMAGE_FORMAT` <-> extension <-> UI index) into a single table.
- Improve `saveImageToPath` fallback: use current `ICAP_IMAGEFILEFORMAT` when the extension is unrecognized, instead of hard-coding PNG.
- Return finer-grained condition codes for disk full, permission denied, or directory creation failure.
- Add `TW_SETUPFILEXFER2` support for long paths.
- Integration-test with TWACK, XnView, NAPS2, and similar applications; document the `DAT_SETUPFILEXFER` / `DAT_IMAGEFILEXFER` ordering each application uses.
- Provide a more visible cue in the settings UI when File Transfer is app-managed (icon + read-only path preview).

</details>
