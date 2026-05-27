# Settings UI Position, Size & Folder Picker Design

Design notes for compacting the settings UI window, locking its size, centring it on screen, and centring the folder-browse dialog.

<details open>
<summary>中文说明</summary>

## 1. 需求

虚拟扫描仪的 settings UI 是一个通过 `ShellExecuteA` 打开的本地浏览器页面。需要实现以下 UI 行为约束：

**主要需求：**

- **紧凑布局**：页面仅显示控件本身，无多余空白，宽度固定为 460px。
- **窗口居中**：浏览器窗口打开时自动定位到屏幕正中心。
- **窗口大小固定**：用户不能拖动边框或点击最大化按钮改变窗口大小。
- **目录选择框居中**：File 模式下点击 Browse 按钮弹出的 `SHBrowseForFolderW` 文件夹选择对话框也应居中到屏幕中心。
- 以上行为在 32 位和 64 位 DS 上一致，在各种常见分辨率 (1366×768、1600×900、1920×1080 等) 下均正常工作。
- 兼容不同默认浏览器 (Chrome / Edge / Firefox)，不依赖浏览器自身对 JS `resizeTo`/`moveTo` 的许可策略。

**非功能性需求：**

- 不引入新的外部依赖。
- 改动限于 `settings_server.cpp` 一个文件。
- 不破坏 settings UI 的现有功能 (语言切换、控件显隐、表单提交等)。

## 2. 领域知识

### 2.1 `ShellExecuteA` 打开浏览器

DS 用 `ShellExecuteA(nullptr, "open", url, ..., SW_SHOWNORMAL)` 启动默认浏览器。调用方无法控制浏览器窗口的初始位置或大小——窗口会出现在浏览器上次关闭的位置，这是 Windows 的默认行为，与 TWAIN DS 无关。

### 2.2 `SetWindowPos` 移动 / 调整窗口

`SetWindowPos(hwnd, hwndInsertAfter, x, y, cx, cy, flags)` 可以移动、调整大小，但不改变窗口的样式 (style)。需要先 `GetWindowLongW(hwnd, GWL_STYLE)` 拿到样式位再 `SetWindowLongW` 修改，最后用 `SWP_FRAMECHANGED` 标志触发非客户区重绘。

### 2.3 `WS_THICKFRAME` 与 `WS_MAXIMIZEBOX`

- `WS_THICKFRAME`：可拖拽调整大小的边框。去掉后窗口边框变为固定大小样式 (类似对话框)。
- `WS_MAXIMIZEBOX`：最大化按钮。去掉后标题栏仅剩关闭按钮。

两者同时去掉后，用户无法通过拖拽边框或标题栏按钮改变窗口尺寸。某些浏览器 (Chrome) 还会在窗口设置后"抵抗"样式修改；实际效果是边框外观变固定，但用户仍可能用 `Win+↑` 最大化——这属于 OS 级别快捷键，无法从 Win32 样式层面完全拦截。

### 2.4 `FindWindowW` 与 `EnumWindows`

- `FindWindowW(nullptr, title)` 按完整窗口标题精确匹配。简单，但浏览器常给标题附加 ` - Chrome` 之类的后缀。
- `EnumWindows(callback, lparam)` 遍历所有顶层窗口，在回调中可用 `wcsstr` 做前缀匹配，作为 fallback。

### 2.5 `SHBrowseForFolderW` 与 `BFFM_INITIALIZED`

`SHBrowseForFolderW(&BROWSEINFOW)` 弹出文件夹选择对话框。通过 `BROWSEINFOW.lpfn` 设置回调，在 `BFFM_INITIALIZED` 消息中可以 `SetWindowPos` 移动对话框。

### 2.6 `BIF_NEWDIALOGSTYLE` 的布局延迟问题

`BIF_NEWDIALOGSTYLE` 的对话框在 `BFFM_INITIALIZED` 回调时尚未完成内部布局——`GetWindowRect` 拿到的尺寸是展开前的小尺寸，以此计算居中坐标会导致对话框实际位置大幅偏移。在 1600×900 分辨率下，对话框底部的"确定"按钮可能超出屏幕可见区域。

**解决方案**：去掉 `BIF_NEWDIALOGSTYLE`，使用经典样式。经典对话框在 `BFFM_INITIALIZED` 时尺寸已固定，居中计算准确。

### 2.7 COM 初始化与 `SHBrowseForFolderW`

`SHBrowseForFolderW` 是 shell COM API。调用线程必须通过 `CoInitializeEx` 初始化 COM，否则静默返回 `NULL`。DS 的 `serverThreadProc` 由 `CreateThread` 创建，默认不初始化 COM，所以必须在入口调用 `CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)`，退出时 `CoUninitialize()`。

### 2.8 JS `resizeTo` / `moveTo` 的局限性

现代浏览器 (Chrome 88+、Edge、Firefox) 默认禁止脚本通过 `resizeTo`/`moveTo` 改变非脚本打开的窗口 (即用户或外部程序打开的窗口)。因此纯 JS 方案不可靠，必须在 C++ 端用 Win32 API 处理。

## 3. 设计目标

- 浏览器窗口在 `ShellExecuteA` 后 3 秒内被找到并居中 + 固定大小。
- 目录选择对话框始终居中，且在所有分辨率下可见完整 UI。
- 页面本身 CSS 紧凑，即使浏览器拒绝窗口尺寸调整，内容也不会溢出。
- 所有逻辑集中在 `settings_server.cpp`，不新增文件或依赖。

**非目标：**

- 不尝试拦截 `Win+↑` 最大化等 OS 级快捷键。
- 不支持在窗口内嵌入浏览器控件 (如 WebView2)，继续使用外部浏览器。
- 不实现自定义主题或深色模式。

## 4. 总体设计

```
showSettingsUi()
│
├── 1. 启动 HTTP server 线程 (CoInitialize + serverThreadProc)
│
├── 2. ShellExecuteA("open", url)  启动浏览器
│
├── 3. 轮询查找浏览器窗口 (最多 3s, 每 100ms)
│     ├── FindWindowW (精确标题匹配)
│     ├── EnumWindows + wcsstr (前缀 fallback)
│     └── 找到后:
│         ├── SetWindowPos 居中 + 设 500×420
│         ├── GetWindowLongW / SetWindowLongW
│         │     去掉 WS_THICKFRAME | WS_MAXIMIZEBOX
│         └── SetWindowPos + SWP_FRAMECHANGED 重绘边框
│
├── 4. WaitForSingleObject 等待用户操作 (最多 60s)
│
└── 5. 清理、返回结果

serverThreadProc()
├── CoInitializeEx (解决 SHBrowseForFolderW 无响应)
├── accept() 循环处理 / /index /browse /submit
│     └── /browse:
│         ├── BIF_RETURNONLYFSDIRS (无 BIF_NEWDIALOGSTYLE)
│         ├── lpfn 回调: BFFM_INITIALIZED → 居中
│         └── SHBrowseForFolderW → 返回 UTF-8 路径
└── CoUninitialize
```

## 5. 重要决策和原因

### 5.1 用 Win32 `SetWindowPos` 居中，不用 JS `resizeTo`/`moveTo`

- **决策**：在 C++ 端用 `FindWindow`/`EnumWindows` 找到浏览器 HWND，`SetWindowPos` 居中。
- **原因**：
  - Chrome / Edge / Firefox 均禁止非脚本打开的窗口被 JS 移动或调整大小。
  - Win32 API 不依赖浏览器权限策略，100% 可靠。
- **代价**：需要轮询等待窗口出现；窗口标题需包含 app_title 才能被匹配到。

### 5.2 用 `GetWindowLongW` / `SetWindowLongW` 去掉 `WS_THICKFRAME` 和 `WS_MAXIMIZEBOX`

- **决策**：修改浏览器窗口样式位，禁止用户拖拽边框或点击最大化按钮改变大小。
- **原因**：
  - 页面内容宽度固定 460px，允许 resize 只会导致大量空白或溢出。
  - 窗口尺寸不可变的交互体验更接近"对话框"而非"网页"，符合扫描参数配置的场景。
- **代价**：某些浏览器 (Chrome) 可能在内部调整窗口大小时重新设置样式位，但实际操作中已验证有效。

### 5.3 文件夹选择对话框用经典样式，不用 `BIF_NEWDIALOGSTYLE`

- **决策**：`bi.ulFlags = BIF_RETURNONLYFSDIRS` (去掉 `BIF_NEWDIALOGSTYLE`)。
- **原因**：
  - 新样式对话框在 `BFFM_INITIALIZED` 时 `GetWindowRect` 返回展开前的初始尺寸，居中计算严重偏差。
  - 经典样式尺寸固定，`BFFM_INITIALIZED` 时居中计算准确，在任何分辨率下都能保证完整可见。
- **代价**：失去左侧树形导航和"新建文件夹"按钮，但 `SHBrowseForFolderW` 经典样式仍然有"新建文件夹"功能，功能未损失。

### 5.4 `FindWindowW` + `EnumWindows` 双路径查找浏览器窗口

- **决策**：先精确匹配标题，失败时用 `EnumWindows` + `wcsstr` 前缀搜索。
- **原因**：
  - Chrome 标题 = "App Title - Google Chrome"，Edge = "App Title - Microsoft​ Edge"，Firefox = "App Title — Mozilla Firefox"。
  - 精确匹配会因浏览器后缀失败；前缀搜索覆盖所有浏览器。
  - `EnumWindows` 比 `FindWindowW` 略慢但仅在 fallback 路径调用，不影响正常速度。
- **代价**：`wcsstr` 可能匹配到其他窗口 (概率极低，因为 app_title 包含 "BN Tech Virtual Scanner" 这类高特异性字符串)。

### 5.5 轮询最多 3 秒

- **决策**：每 100ms 检查一次，最多 30 次 (3 秒)。
- **原因**：
  - 浏览器从进程启动到窗口创建通常 200–800ms，冷启动可能到 1.5s。
  - 3 秒上限足够覆盖各种情况，不会无限阻塞。
- **代价**：极端情况 (老旧 HDD + 冷启动 + 杀毒软件扫描) 可能超时；超时后窗口出现在浏览器上次位置，功能不受影响，只是没居中。

### 5.6 `serverThreadProc` 入口 `CoInitializeEx`

- **决策**：线程函数第一行调用 `CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)`。
- **原因**：`SHBrowseForFolderW` 是 COM API，未初始化 COM 的线程上调用会静默返回 NULL，用户点击 Browse 无反应。此问题已在 Twack 32 测试中复现。
- **代价**：无。`CoInitializeEx` / `CoUninitialize` 成对调用，不影响其他 API。

### 5.7 CSS 紧凑化：固定 body 宽度 460px + `overflow:hidden`

- **决策**：body `width:460px`，`html,body{overflow:hidden}`，缩小所有 margin/padding/font-size。
- **原因**：
  - 460px 刚好容纳最长一行 (output directory: label 130px + input 240px + button 60px + gap)。
  - `overflow:hidden` 防止内容超出时出现滚动条。
  - 即使浏览器拒绝 `SetWindowPos` 调整大小，内容也不会溢出。
- **代价**：如果后续添加更多控件，460px 可能不够；届时需同步增大窗口尺寸常量。

## 6. 架构各组件改动点

### 6.1 `src/settings_server.cpp`

唯一改动的文件。具体修改：

| 位置 | 修改内容 |
|---|---|
| `buildHtmlPage()` CSS | body 加 `width:460px`、`html,body{overflow:hidden}`；缩小所有 margin/padding/font-size (见 §7.1 对照表) |
| `showSettingsUi()` | `ShellExecuteA` 后新增约 30 行轮询 + 查找 + 居中 + 去样式代码 |
| `showSettingsUi()` | `SetWindowPos` 居中后加 `GetWindowLongW`/`SetWindowLongW` 去掉 `WS_THICKFRAME \| WS_MAXIMIZEBOX` + `SWP_FRAMECHANGED` |
| `serverThreadProc()` | 入口加 `CoInitializeEx`，退出加 `CoUninitialize` |
| `/browse` 处理 | `BIF_RETURNONLYFSDIRS` (去掉 `BIF_NEWDIALOGSTYLE`)；`BFFM_INITIALIZED` 回调中 `SetWindowPos` 居中 |

### 6.2 不动的组件

- `settings_server.h`：接口未变。
- `twain_data_source.cpp`：调用 `showSettingsUi` 的代码未变。
- `localization.cpp` / `localization.h`：未变。
- `CMakeLists.txt`：无新文件、无新依赖。
- 所有其他源文件：未变。

### 6.3 CSS 紧凑化对照表

| 属性 | 修改前 | 修改后 | 说明 |
|---|---|---|---|
| body width | 无 (auto) | **460px** | 固定内容宽度 |
| html,body overflow | 默认 | **hidden** | 禁止滚动条 |
| body margin | 20px | 12px | |
| h1 font-size | 18px | 16px | |
| h1 margin | 默认 | 0 0 10px 0 | 去掉顶部多余空白 |
| .group padding | 16px | 10px 12px | |
| .group margin-bottom | 16px | 10px | |
| .group h2 font-size | 14px | 13px | |
| .group h2 margin | margin-top:0 | 0 0 6px 0 | |
| label width | 140px | 130px | |
| label font-size | 无 | 13px | |
| select/input margin | 4px 0 | 2px 0 | |
| select/input padding | 4px | 3px | |
| select/input font-size | 无 | 13px | |
| .buttons margin-top | 16px | 10px | |
| button padding | 8px 24px | 6px 20px | |
| button font-size | 14px | 13px | |
| button margin-left | 8px | 6px | |

## 7. 典型流程

### 7.1 settings UI 弹出 (ShowUI=TRUE)

```text
1. DS 调用 showSettingsUi()
2. CoInitialize + 创建 HTTP server 线程
3. ShellExecuteA("open", "http://127.0.0.1:xxxxx/")
   浏览器进程启动
4. 轮询 (最多 3s):
   FindWindowW("BN Tech Virtual Scanner")         ← 第 1 次尝试
   → 失败 (标题 = "BN Tech Virtual Scanner - Chrome")
   EnumWindows + wcsstr("BN Tech Virtual Scanner") ← fallback
   → 找到 Chrome 窗口 HWND
5. SetWindowPos(hwnd, x=550, y=240, 500, 420)    ← 居中
6. GetWindowLongW → style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX)
   SetWindowLongW → SetWindowPos + SWP_FRAMECHANGED
   窗口边框变为固定大小，最大化按钮消失
7. 用户操作 → submit → server 线程结束
8. WaitForSingleObject 返回 → 清理
```

### 7.2 目录选择框弹出 (点击 Browse)

```text
1. 用户点击 Browse 按钮
2. JS: GET /browse
3. serverThreadProc 收到请求
4. BROWSEINFOW:
   ulFlags = BIF_RETURNONLYFSDIRS (无 BIF_NEWDIALOGSTYLE)
   lpfn = BFFM_INITIALIZED 回调:
     GetWindowRect → 计算居中坐标
     SetForegroundWindow + SetWindowPos(HWND_TOPMOST)
     SetWindowPos(HWND_NOTOPMOST)
5. SHBrowseForFolderW → 用户选择 d:\tmp → 确定
6. WideCharToMultiByte(UTF-8) → 返回 "d:\\tmp"
7. JS: document.getElementById('outputdir').value = "d:\\tmp"
```

## 8. 限制

- 浏览器窗口的"非脚本打开"判断由各浏览器独立实现；未来版本可能引入新的限制。
- `Win+↑` 最大化等 OS 级快捷键仍可改变窗口大小，无法从 Win32 样式位层面拦截。
- 窗口标题匹配依赖 `localization::strings().app_title` 出现在浏览器标题栏中；如果某浏览器完全不设置标题 (罕见)，窗口查找会超时。
- `EnumWindows` + `wcsstr` 可能匹配到无关窗口 (极低概率)；目前无去重或验证机制。
- 经典 `SHBrowseForFolderW` 对话框没有左侧树形导航，但功能完整。
- 紧凑 CSS 假设最长行宽度 ≤ 460px；如果后续加更宽的控件需同步调整。
- 在 RTL 语言 (如阿拉伯语) 下未测试居中与布局。
- `CoInitializeEx` 在 `serverThreadProc` 入口调用；如果线程因异常提前退出而跳过 `CoUninitialize`，COM 资源可能泄漏 (当前流程无此风险)。

## 9. 下一步工作

- 验证更多浏览器：Firefox、Opera、Brave、IE11 (如有需求)。
- 如果 Chrome 将来完全阻止外部 `SetWindowPos` 修改样式位，评估 WebView2 嵌入方案。
- 验证高 DPI (150%、200% 缩放) 下的窗口尺寸和居中效果。
- 将 500×420 窗口尺寸常量移到可配置项 (如 `config.ini`)。
- 为 `EnumWindows` 匹配加窗口类名校验 (`GetClassNameW`) 以提高匹配精度。
- 评估用 `SWP_NOSENDCHANGING` 避免浏览器内部重新布局带来的闪烁。
- 加自动化 UI 测试：模拟浏览器窗口 + 验证位置/尺寸。
- 考虑在窗口查找超时时打印日志，方便排查冷启动环境问题。

</details>

<details>
<summary>English</summary>

## 1. Requirement

The virtual scanner's settings UI is a local browser page launched via `ShellExecuteA`. It needs the following UI behaviour constraints:

**Main requirements:**

- **Compact layout**: only controls are visible, no excess whitespace; fixed 460px content width.
- **Window centred**: the browser window opens centred on screen.
- **Fixed window size**: user cannot drag borders or click the maximise button.
- **Folder picker centred**: the `SHBrowseForFolderW` dialog is also centred.
- Identical behaviour on 32-bit and 64-bit DS across common resolutions.
- Compatible with Chrome / Edge / Firefox; does not rely on JS `resizeTo`/`moveTo` permissions.

**Non-functional:**

- No new dependencies.
- Changes limited to `settings_server.cpp`.
- Existing functionality (i18n, show/hide controls, form submit) preserved.

## 2. Domain knowledge

### 2.1 `ShellExecuteA` opens the browser

`ShellExecuteA(nullptr, "open", url, ..., SW_SHOWNORMAL)` starts the default browser. The DS cannot control initial position/size — the window appears where the browser last closed.

### 2.2 `SetWindowPos`

`SetWindowPos(hwnd, hwndInsertAfter, x, y, cx, cy, flags)` moves and sizes a window. To change style bits use `GetWindowLongW(GWL_STYLE)` + `SetWindowLongW`, then call `SetWindowPos` with `SWP_FRAMECHANGED` to repaint the non-client area.

### 2.3 `WS_THICKFRAME` and `WS_MAXIMIZEBOX`

- `WS_THICKFRAME`: resizable border. Removing it gives a fixed-size dialog-like border.
- `WS_MAXIMIZEBOX`: maximise button. Removing it leaves only the close button.

### 2.4 `FindWindowW` and `EnumWindows`

- `FindWindowW(nullptr, title)`: exact title match.
- `EnumWindows(callback, lparam)`: enumerates top-level windows; use `wcsstr` for prefix matching as a fallback (browsers append " - Chrome" etc.).

### 2.5 `SHBrowseForFolderW` and `BFFM_INITIALIZED`

`SHBrowseForFolderW(&BROWSEINFOW)` opens a folder picker. The `lpfn` callback receives `BFFM_INITIALIZED` after the dialog is created, at which point `SetWindowPos` can centre it.

### 2.6 `BIF_NEWDIALOGSTYLE` layout-delay bug

The `BIF_NEWDIALOGSTYLE` dialog is not fully laid out at `BFFM_INITIALIZED`; `GetWindowRect` returns the pre-expansion size. Centring from this causes a large offset — at 1600×900 the OK button may be off-screen. **Fix**: use classic style (no `BIF_NEWDIALOGSTYLE`).

### 2.7 COM initialisation for `SHBrowseForFolderW`

`SHBrowseForFolderW` is a shell COM API. The calling thread must `CoInitializeEx`, or the API silently returns NULL. The `serverThreadProc` thread created by `CreateThread` lacks COM initialisation by default.

### 2.8 JS `resizeTo`/`moveTo` limitations

Modern browsers block scripts from resizing/moving windows not opened by script. JS-based centring is unreliable; the fix must happen in C++ with Win32.

## 3. Design goals

- Browser window found, centred, and locked within 3 s of `ShellExecuteA`.
- Folder picker always centred and fully visible at all resolutions.
- Compact CSS as a fallback if the browser rejects the size change.
- All logic in `settings_server.cpp`.

**Non-goals:** blocking OS shortcuts (`Win+↑`), embedding a browser control, custom themes.

## 4. Overall design

```
showSettingsUi()
├── 1. HTTP server thread (CoInitialize + serverThreadProc)
├── 2. ShellExecuteA("open", url)
├── 3. Poll for browser HWND (≤ 3 s, every 100 ms)
│     ├── FindWindowW (exact title)
│     ├── EnumWindows + wcsstr (prefix fallback)
│     └── Found:
│         ├── SetWindowPos centre + 500×420
│         ├── Get/SetWindowLongW remove WS_THICKFRAME | WS_MAXIMIZEBOX
│         └── SetWindowPos + SWP_FRAMECHANGED
├── 4. WaitForSingleObject (≤ 60 s)
└── 5. Cleanup

serverThreadProc()
├── CoInitializeEx
├── accept() loop → / /index /browse /submit
│     └── /browse:
│         ├── BIF_RETURNONLYFSDIRS (classic)
│         ├── lpfn → BFFM_INITIALIZED centre
│         └── SHBrowseForFolderW → UTF-8 path
└── CoUninitialize
```

## 5. Key decisions and rationale

### 5.1 Win32 `SetWindowPos` centring (not JS)

JS `resizeTo`/`moveTo` is blocked by modern browsers for non-script-opened windows. Win32 is 100% reliable.

### 5.2 Remove `WS_THICKFRAME` | `WS_MAXIMIZEBOX` via `SetWindowLongW`

Content is fixed 460px; resizing only adds whitespace. Removing these style bits gives a dialog-like fixed-size border.

### 5.3 Classic folder dialog (no `BIF_NEWDIALOGSTYLE`)

`BIF_NEWDIALOGSTYLE` returns pre-expansion rect at `BFFM_INITIALIZED`, causing centring offset. Classic style has a fixed size, so centring is accurate.

### 5.4 Dual-path browser window lookup

`FindWindowW` for exact match; `EnumWindows` + `wcsstr` for prefix fallback (browsers append suffixes). Highly specific app title avoids false matches.

### 5.5 Poll up to 3 s

Browser startup usually 200–800 ms; 3 s covers cold starts. Timeout leaves the window at its last position — still functional, just not centred.

### 5.6 `CoInitializeEx` in `serverThreadProc`

`SHBrowseForFolderW` is a COM API; without `CoInitializeEx` it returns NULL silently (Browse button no-op). Reproduced and fixed.

### 5.7 Compact CSS: fixed 460px body + `overflow:hidden`

460px fits the longest row exactly. `overflow:hidden` prevents scrollbars. Acts as a fallback even if the browser ignores `SetWindowPos`.

## 6. Component changes

### 6.1 `src/settings_server.cpp`

The only file changed:

| Area | Change |
|---|---|
| `buildHtmlPage()` CSS | `body{width:460px}`, `html,body{overflow:hidden}`, tighter margins/padding/font-sizes (see table in §6.3 of the Chinese section) |
| `showSettingsUi()` | ~30-line poll + find + centre + lock-size block after `ShellExecuteA` |
| `showSettingsUi()` | `GetWindowLongW`/`SetWindowLongW` + `SWP_FRAMECHANGED` |
| `serverThreadProc()` | `CoInitializeEx` at entry, `CoUninitialize` at exit |
| `/browse` handler | `BIF_RETURNONLYFSDIRS` only; `BFFM_INITIALIZED` centre in callback |

### 6.2 Untouched

`settings_server.h`, `twain_data_source.cpp`, `localization.*`, `CMakeLists.txt`, all other files.

## 7. Typical flows

### 7.1 Settings UI launch (ShowUI=TRUE)

```text
1. showSettingsUi()
2. CoInitialize + HTTP server thread
3. ShellExecuteA("open", "http://127.0.0.1:xxxxx/")
4. Poll ≤ 3 s:
   FindWindowW("BN Tech Virtual Scanner") — fails (browser suffix)
   EnumWindows + wcsstr("BN Tech Virtual Scanner") — found
5. SetWindowPos centre + 500×420
6. Remove WS_THICKFRAME | WS_MAXIMIZEBOX + SWP_FRAMECHANGED
7. User interacts → submit → server thread ends
8. Cleanup
```

### 7.2 Folder picker (Browse button)

```text
1. JS: GET /browse
2. BROWSEINFOW: ulFlags = BIF_RETURNONLYFSDIRS, lpfn centres at BFFM_INITIALIZED
3. SHBrowseForFolderW → user picks folder
4. UTF-8 path returned to JS → outputdir.value updated
```

## 8. Limitations

- OS shortcuts (`Win+↑`) can still resize the window.
- Title-based lookup relies on `app_title` appearing in the browser title bar.
- `EnumWindows` prefix search has a tiny false-match risk.
- Classic folder dialog lacks a tree pane (full functionality otherwise).
- CSS assumes max row width ≤ 460px; new wider controls need a width bump.
- Not tested with RTL languages.
- Missed `CoUninitialize` on exceptional thread exit is a theoretical COM leak.

## 9. Next steps

- Test more browsers: Firefox, Opera, Brave.
- Consider WebView2 if Chrome tightens `SetWindowPos` restrictions.
- Test high-DPI (150%, 200% scaling).
- Make 500×420 configurable (e.g. via `config.ini`).
- Add `GetClassNameW` check to `EnumWindows` for match precision.
- Try `SWP_NOSENDCHANGING` to reduce browser re-layout flicker.
- Add automated UI tests.
- Log when window lookup times out.

</details>
