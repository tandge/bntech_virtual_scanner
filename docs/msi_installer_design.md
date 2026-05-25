# MSI Installer Design

Design notes for adding WiX 4 based MSI packaging for BN Tech Virtual Scanner.

<details open>
<summary>中文说明</summary>

## 1. 需求

项目需要基于 WiX Toolset 4.0.4 生成 32 位和 64 位两个 MSI 安装包，每个 MSI 内置英文和简体中文两种 UI 语言。

生成目标：

```text
build\installer\win32\bntech_virtual_scanner_win32.msi
build\installer\win64\bntech_virtual_scanner_win64.msi
```

MSI 安装内容需要和 `build.bat install` / `cmake --install` 保持一致：

- `bntech_virtual_scanner.ds`
- `FreeImage.dll`
- `TWAIN_logo.png`

安装目标目录：

```text
32-bit: C:\Windows\twain_32\bntech\
64-bit: C:\Windows\twain_64\bntech\
```

MSI 支持多语言 UI：同一个 MSI 在中文 Windows 上自动显示中文界面并写入 `%APPDATA%\bntech\config.ini`，在英文 Windows 上显示英文界面，不写 config.ini（DS 默认为 `en_US`）。

## 2. 领域知识

### 2.1 TWAIN Data Source 安装位置

Windows TWAIN Data Source 是一个特殊 DLL，扩展名通常是 `.ds`。32 位和 64 位 TWAIN 数据源安装目录不同：

| 架构 | 安装目录 |
|---|---|
| 32-bit | `C:\Windows\twain_32\bntech\` |
| 64-bit | `C:\Windows\twain_64\bntech\` |

这两个目录都位于 Windows 系统目录下，因此安装通常需要管理员权限。

### 2.2 WiX 4 构建模型

WiX 4 使用 `wix build` 直接从 `.wxs` 文件生成 MSI。本项目新增：

```text
installer\bntech_virtual_scanner.wxs
```

CMake 通过自定义 CMake 函数 `add_multilang_msi` 调用 WiX 两次（英文 + 中文），然后用 Windows SDK 工具合并为多语言 MSI。

### 2.3 MSI 嵌入语言转换

MSI 本身只支持单一语言 UI。多语言 MSI 是通过嵌入语言转换文件 （`.mst`） 实现的，Windows Installer 运行时根据系统语言自动选择对应转换。

实现使用 Windows SDK 三个工具：

| 工具 | 用途 |
|---|---|
| `MsiTran.exe` | 从两个单语言 MSI 生成 `.mst` 转换文件 |
| `WiSubStg.vbs` | 将 `.mst` 嵌入到 MSI 子存储 |
| `WiLangId.vbs` | 更新 MSI 摘要信息，声明支持的语言列表 |

构建流程：

```text
wix build ... -culture en-us → win32_en_US.msi
wix build ... -culture zh-cn → win32_zh_CN.msi
MsiTran.exe -g en.msi zh.msi zh_CN.mst
copy en.msi → final.msi
WiSubStg.vbs final.msi zh_CN.mst 2052
WiLangId.vbs final.msi Package 1033,2052
cleanup en.msi zh.msi zh_CN.mst
```

`-culture` 参数使 WiX UI 扩展自动选择对应语言的标准向导文本。

### 2.4 WiX UI 扩展

在当前环境中，WixToolset.UI.wixext 4.0.6 可用。MSI 基于 `WixUI_InstallDir` 标准向导，提供：

- Welcome / License 页面
- 安装目录选择
- 安装就绪确认
- 安装进度条
- 完成页面

### 2.5 `%APPDATA%` 与安装上下文

MSI 中使用 `AppDataFolder` 表示当前安装上下文下的 AppData roaming 目录。本项目用它创建：

```text
%APPDATA%\bntech\
```

并在 `APP_LANGUAGE != en_US` 时写入 `config.ini`。

注意：如果以管理员身份为所有用户安装，`AppDataFolder` 的实际用户上下文可能与最终运行扫描应用的用户有关，后续需要进一步验证多用户场景。

## 3. 设计目标

- 使用 WiX 4.0.4 生成 MSI。
- 同时支持 32 位和 64 位安装包。
- 单个 MSI 内置英文和简体中文 UI，安装时自动根据系统语言显示。
- MSI 安装内容与现有 build/install 流程一致。
- CMake 暴露 `msi32` 和 `msi64` target。
- `build.bat msi32` / `build.bat msi64` 可单独生成 MSI。
- `build.bat` 不带参数时仍保留自动构建、安装到 `C:\Windows` 的行为，并额外生成两个 MSI。
- MSI 支持通过 `APP_LANGUAGE` 写入语言配置。
- MSI 使用 WixUI_InstallDir 标准安装向导。

非目标：

- 当前不提供运行时语言选择页面（MSI 不支持单包运行时切换 UI 语言，语言通过嵌入转换实现）。
- 当前不打包 VC Runtime 或 WiX bootstrapper。
- 当前不处理所有用户的 `%APPDATA%` 配置同步。
- 当前没有安装/卸载成功或失败消息弹框。

## 4. 总体设计

新增 WiX 源文件：

```text
installer\bntech_virtual_scanner.wxs
```

CMake 新增自定义 target 和构建函数：

```text
msi32
msi64
add_multilang_msi()
```

`build.bat` 新增命令：

```bat
build.bat msi32
build.bat msi64
```

不带参数时：

```bat
build.bat
```

执行完整流程：

```text
build win32
build win64
install win32 to C:\Windows\twain_32\bntech
install win64 to C:\Windows\twain_64\bntech
build msi32 (en-us + zh-cn → merged)
build msi64 (en-us + zh-cn → merged)
```

## 5. 重要决策和原因

### 5.1 32 位和 64 位 MSI 分开生成

决策：生成两个独立 MSI，而不是一个同时包含 32 位和 64 位组件的 MSI。

原因：

- TWAIN 32 位和 64 位目录不同。
- WiX/MSI 对组件架构有明确区分。
- 分开生成更简单、风险更低、易于测试。

### 5.2 MSI 安装文件来自 CMake build 输出目录

决策：WiX 的 `SourceDir` 指向：

```text
build\win32
build\win64
```

原因：

- CMake build 后这些目录已经包含 `.ds`、`FreeImage.dll`、`TWAIN_logo.png`。
- 与 `build.bat install` 的文件来源一致。
- 避免 WiX 重复理解项目源码和依赖路径。

### 5.3 使用嵌入语言转换实现多语言

决策：通过 Windows SDK 工具（MsiTran / WiSubStg / WiLangId）将两个单语言 MSI 合并为一个多语言 MSI。

原因：

- MSI 标准不支撑运行时 UI 语言切换。
- 嵌入转换是 Windows Installer 原生支持的多语言机制。
- 用户安装时无需额外操作，系统语言自动匹配。
- 仍可强制指定语言：`msiexec /i xxx.msi TRANSFORMS=:2052`。

### 5.4 使用 WixUI_InstallDir 标准向导

决策：引入 `WixToolset.UI.wixext`，使用 `WixUI_InstallDir`。

原因：

- 在当前环境中扩展 4.0.6 可用，不再有找不到扩展的问题。
- 提供完整的安装目录选择、进度条和完成页面。
- 配合 `-culture` 参数自动提供中文或英文标准向导文本。

### 5.5 不弹安装/卸载结果消息框

决策：移除全部 VBScript CustomAction 消息弹框。

原因：

- WixUI_InstallDir 已有进度条和完成页面，用户可见安装结果。
- 免除了 VBScript 在企业环境和静默安装模式下的兼容性问题。
- 简化了构建流程和文件维护。

### 5.6 使用编译期 AppLanguage 预处理变量

决策：`APP_LANGUAGE` 在 WiX 编译期通过 `$(var.AppLanguage)` 预处理变量写死。

原因：

- 与嵌入转换方案配合，每种语言 MSI 自带对应的 `APP_LANGUAGE` 值。
- 用户无需通过命令行传入语言参数。
- `APP_LANGUAGE` 控制 `config.ini` 写入逻辑：`en_US` 不写，`zh_CN` 写入。

## 6. 架构各层改动

### 6.1 installer 层

新增：

```text
installer\bntech_virtual_scanner.wxs
```

`bntech_virtual_scanner.wxs` 负责：

- 根据 `$(var.Platform)` 定义 32/64 位产品名、TWAIN 目录和 UpgradeCode。
- 根据 `$(var.AppLanguage)` 定义产品显示名和内置 `APP_LANGUAGE` 属性。
- 定义安装文件组件：`.ds`, `FreeImage.dll`, `TWAIN_logo.png`。
- 定义 `%APPDATA%\bntech` 目录。
- 在 `APP_LANGUAGE != en_US` 时写 `config.ini`。
- 使用 `WixUI_InstallDir` 标准向导。

### 6.2 CMake 层

文件：

```text
CMakeLists.txt
```

新增 CMake 函数和变量：

- `WIX_EXECUTABLE` / `WIX_UI_EXTENSION` / `WIX_SOURCE_FILE`
- `WINSDK_BIN` / `MSITRAN` / `WISUBSTG` / `WILANGID`
- `add_multilang_msi(msi64 x64 x64 ...)`
- `add_multilang_msi(msi32 x86 win32 ...)`

每个 `add_multilang_msi` 调用执行：

1. `wix build` 英文 MSI（`-culture en-us`）
2. `wix build` 中文 MSI（`-culture zh-cn`）
3. `MsiTran.exe -g` 生成语言转换
4. 复制英文 MSI 为最终文件名
5. `WiSubStg.vbs` 嵌入转换（LCID 2052）
6. `WiLangId.vbs` 声明多语言（1033,2052）
7. 清理中间文件

### 6.3 build.bat 层

文件：

```text
build.bat
```

新增：

- 参数 `msi32`
- 参数 `msi64`
- `:msi` 子过程

不带参数行为：

```text
build both → install both → package both MSI
```

### 6.4 i18n 配置层

MSI 不直接修改 DS 代码，而是写入 DS 已支持的配置文件：

```text
%APPDATA%\bntech\config.ini
```

这使 installer 与运行时 i18n 逻辑保持松耦合。

## 7. 安装和打包流程

### 7.1 不带参数完整流程

```text
build.bat
  → 请求管理员权限
  → 构建 win32
  → 构建 win64
  → cmake --install build/win32 --prefix C:/Windows
  → cmake --install build/win64 --prefix C:/Windows
  → cmake --build build/win32 --target msi32
  → cmake --build build/win64 --target msi64
```

### 7.2 单独生成 MSI

```text
build.bat msi32
  → 配置 build/win32
  → cmake --build build/win32 --target msi32
```

```text
build.bat msi64
  → 配置 build/win64
  → cmake --build build/win64 --target msi64
```

### 7.3 安装及语言

自动匹配系统语言：

```bat
msiexec /i bntech_virtual_scanner_win64.msi
```

强制中文：

```bat
msiexec /i bntech_virtual_scanner_win64.msi TRANSFORMS=:2052
```

## 8. 局限性

- MSI 不可运行时切换 UI 语言（单 MSI 包的限制）。语言通过一篇嵌入转换覆盖，安装时按系统语言自动选择。
- `AppDataFolder` 在管理员安装和多用户环境中的实际落点需要进一步测试。
- 当前没有安装/卸载成功或失败消息弹框。用户通过 WixUI 进度条和完成页面了解安装状态。
- 当前没有自动检查 WiX 版本。
- 当前没有把 MSI 构建纳入 CI。
- 删除 `.vbs` 后，静默安装模式完全无声（无弹框），这在某些场景下可能是优点。

## 9. 下一步工作

- 增加 WiX 版本和 Windows SDK 工具版本检测。
- 增加安装前检查：目标 `.ds` 是否被扫描应用占用。
- 增加安装日志输出说明，例如提示用户使用 `/l*v install.log`。
- 测试管理员安装、多用户登录和不同 `%APPDATA%` 上下文。
- 支持卸载时清理或保留 config.ini 的明确策略。
- 在 CI 中生成并归档 win32/win64 MSI。
- 评估是否需要更新 MSI 内部的 `APP_LANGUAGE` 注释（当前仍写 "baked-in"）。

</details>

<details>
<summary>English</summary>

## 1. Requirement

The project needs WiX Toolset 4.0.4 based MSI packages for 32-bit and 64-bit builds. Each MSI supports both English and Simplified Chinese UI via embedded language transforms.

Outputs:

```text
build\installer\win32\bntech_virtual_scanner_win32.msi
build\installer\win64\bntech_virtual_scanner_win64.msi
```

The MSI installs the same files as `build.bat install` / `cmake --install`:

- `bntech_virtual_scanner.ds`
- `FreeImage.dll`
- `TWAIN_logo.png`

Target directories:

```text
32-bit: C:\Windows\twain_32\bntech\
64-bit: C:\Windows\twain_64\bntech\
```

The MSI auto-detects the system language: Chinese Windows shows Chinese UI and writes `%APPDATA%\bntech\config.ini` with `language=zh_CN`; English Windows shows English UI and writes no config.ini (the data source defaults to `en_US`).

## 2. Domain knowledge

A TWAIN Data Source is a DLL-like `.ds` file. 32-bit and 64-bit TWAIN sources are installed in different Windows directories. Because the target is under `C:\Windows`, installation usually requires administrator privileges.

WiX 4 uses `wix build` to generate MSI files from `.wxs` source. This project adds `installer\bntech_virtual_scanner.wxs`. CMake builds two single-language MSIs and merges them into one multi-language package using Windows SDK tools.

The Windows Installer does not support switching the built-in UI language at runtime in a single MSI. Multi-language MSIs use embedded transforms (`.mst`), which means building two single-language MSIs and merging them:

| Tool | Purpose |
|---|---|
| `MsiTran.exe` | Generate `.mst` transform from two single-language MSIs |
| `WiSubStg.vbs` | Embed `.mst` into MSI sub-storage |
| `WiLangId.vbs` | Update MSI summary info with supported language list |

Build flow:

```text
wix build ... -culture en-us → win32_en_US.msi
wix build ... -culture zh-cn → win32_zh_CN.msi
MsiTran.exe -g en.msi zh.msi zh_CN.mst
copy en.msi → final.msi
WiSubStg.vbs final.msi zh_CN.mst 2052
WiLangId.vbs final.msi Package 1033,2052
cleanup en.msi zh.msi zh_CN.mst
```

The `-culture` parameter tells WiX UI extension to use the matching language for standard wizard text.

The project uses `WixToolset.UI.wixext` with `WixUI_InstallDir`, which provides a full installation wizard with directory selection, progress, and completion pages. The `-culture` parameter selects the matching language for the wizard.

## 3. Design goals

- Build MSI packages with WiX 4.0.4.
- Support separate 32-bit and 64-bit MSI outputs.
- Single MSI with embedded English + Chinese UI, auto-selected by system language.
- Install the same files as the existing install flow.
- Provide CMake targets `msi32` and `msi64`.
- Support `build.bat msi32` and `build.bat msi64`.
- Keep no-argument `build.bat` behavior: build, install to `C:\Windows`, and package both MSI files.
- Support language configuration via `APP_LANGUAGE` baked into each transform.
- Use WixUI_InstallDir standard wizard.

Non-goals:

- No runtime language selection page (MSI limitation; language is selected via embedded transforms).
- No VC Runtime/bootstrapper packaging yet.
- No all-user AppData synchronization yet.
- No install/uninstall success/failure message boxes.

## 4. Overall design

Added installer file:

```text
installer\bntech_virtual_scanner.wxs
```

Added CMake targets and functions:

```text
msi32
msi64
add_multilang_msi()
```

Added build script commands:

```bat
build.bat msi32
build.bat msi64
```

No-argument `build.bat` now performs:

```text
build win32
build win64
install win32
install win64
build msi32 (en-us + zh-cn → merged)
build msi64 (en-us + zh-cn → merged)
```

## 5. Key decisions and rationale

### 5.1 Separate MSI per architecture

Separate MSI packages keep the TWAIN architecture-specific directories simple and avoid mixed-architecture component complexity.

### 5.2 Use CMake build output as WiX source

WiX packages files from `build\win32` or `build\win64`, where CMake already places `.ds`, `FreeImage.dll`, and `TWAIN_logo.png`.

### 5.3 Embedded language transforms for multi-language

Decision: merge two single-language MSIs into one using Windows SDK tools.

Rationale:

- MSI cannot switch built-in UI text at runtime.
- Embedded transforms are the native Windows Installer mechanism for multi-language.
- Users get the right language automatically with no extra steps.
- Users can still force a language: `msiexec /i xxx.msi TRANSFORMS=:2052`.

### 5.4 Use WixUI_InstallDir standard wizard

Decision: depend on `WixToolset.UI.wixext` with `WixUI_InstallDir`.

Rationale:

- The extension 4.0.6 is available and reliable in the current build environment.
- Provides a full wizard with directory selection, progress, and completion.
- Combined with `-culture`, the wizard text auto-localizes.

### 5.5 No install/uninstall message boxes

Decision: remove all VBScript custom action message boxes.

Rationale:

- WixUI_InstallDir already provides a progress bar and completion page.
- Avoids VBScript compatibility issues in enterprise and silent-install scenarios.
- Simplifies the build process and file maintenance.

### 5.6 Compile-time AppLanguage preprocessor variable

Decision: `APP_LANGUAGE` is baked in at WiX compile time via `$(var.AppLanguage)`.

Rationale:

- Each language variant naturally carries the correct `APP_LANGUAGE` value.
- Users do not need to pass a language parameter on the command line.
- `APP_LANGUAGE` controls config.ini writing: `en_US` → skip, `zh_CN` → write.

## 6. Component changes

### 6.1 Installer layer

`installer\bntech_virtual_scanner.wxs` defines:

- product name and UpgradeCode per architecture via `$(var.Platform)`;
- product display name and `APP_LANGUAGE` per language via `$(var.AppLanguage)`;
- file components: `.ds`, `FreeImage.dll`, `TWAIN_logo.png`;
- `%APPDATA%\bntech` directory;
- conditional `config.ini` writing;
- `WixUI_InstallDir` standard wizard.

### 6.2 CMake layer

`CMakeLists.txt` adds:

- `WIX_EXECUTABLE`, `WIX_UI_EXTENSION`, `WIX_SOURCE_FILE`
- `WINSDK_BIN`, `MSITRAN`, `WISUBSTG`, `WILANGID`
- `add_multilang_msi()` function

Each `add_multilang_msi` call performs:
1. `wix build` English MSI (`-culture en-us`)
2. `wix build` Chinese MSI (`-culture zh-cn`)
3. `MsiTran.exe -g` to generate transform
4. Copy English MSI as final output
5. `WiSubStg.vbs` to embed transform (LCID 2052)
6. `WiLangId.vbs` to declare multi-language support (1033,2052)
7. Clean up intermediate files

### 6.3 build.bat layer

`build.bat` adds `msi32`, `msi64`, and a `:msi` helper. The no-argument path builds, installs, and packages both architectures.

### 6.4 i18n config layer

The installer does not duplicate runtime i18n logic. It only writes `%APPDATA%\bntech\config.ini`, which the data source already knows how to read.

## 7. Build and install flow

No-argument flow:

```text
build.bat
  → elevate for Windows install
  → build win32 and win64
  → install both to C:\Windows
  → build msi32 and msi64
```

Single MSI flow:

```bat
build.bat msi32
build.bat msi64
```

Installation examples:

```bat
# Auto-detect system language
msiexec /i bntech_virtual_scanner_win64.msi

# Force Simplified Chinese
msiexec /i bntech_virtual_scanner_win64.msi TRANSFORMS=:2052
```

## 8. Limitations

- MSI cannot switch UI language at runtime (single-MSI limitation). Language selection is handled by embedded transforms at install time.
- `%APPDATA%` behavior needs more testing under elevated and multi-user installs.
- No install/uninstall result message boxes. Results are visible through the WixUI progress bar and completion page.
- WiX version is not checked automatically.
- MSI build is not yet part of CI.
- Silent installs are fully silent (no pop-ups), which may be desirable in some cases.

## 9. Next steps

- Add WiX version and Windows SDK tool version checks.
- Add pre-install checks for locked `.ds` files.
- Document install logging, for example `/l*v install.log`.
- Test elevated installs and multi-user AppData behavior.
- Define uninstall behavior for config.ini.
- Add CI jobs to build and archive both MSI files.

</details>
