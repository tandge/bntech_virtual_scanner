# MSI Installer Design

Design notes for adding WiX 4 based MSI packaging for BN Tech Virtual Scanner.

<details open>
<summary>中文说明</summary>

## 1. 需求

项目需要基于 WiX Toolset 4.0.4 生成 32 位和 64 位两个 MSI 安装包。

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

MSI 还需要支持语言配置。默认语言为 `en_US`。当安装时选择或传入的语言不是 `en_US` 时，需要写入：

```text
%APPDATA%\bntech\config.ini
```

写入内容：

```ini
[Settings]
language=zh_CN
```

安装完成后需要给用户成功提示；安装失败并触发回滚时，需要提示安装失败以及可能原因。卸载成功后也需要提示卸载成功；卸载失败并触发回滚时，需要提示卸载失败以及可能原因。

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

CMake 通过自定义 target 调用 WiX：

```bat
cmake --build . --target msi32
cmake --build . --target msi64
```

### 2.3 WiX UI 扩展取舍

最初设计尝试使用 `WixToolset.UI.wixext` 提供安装 UI 和语言选择页。但实际构建环境中出现：

```text
WIX0144: The extension 'WixToolset.UI.wixext' could not be found
```

为了减少环境依赖，当前 MSI 设计不依赖 WiX UI 扩展。语言通过 MSI property 传入：

```bat
msiexec /i bntech_virtual_scanner_win64.msi APP_LANGUAGE=zh_CN
```

如果不传入，则默认 `en_US`。

### 2.4 `%APPDATA%` 与安装上下文

MSI 中使用 `AppDataFolder` 表示当前安装上下文下的 AppData roaming 目录。本项目用它创建：

```text
%APPDATA%\bntech\
```

并在 `APP_LANGUAGE != en_US` 时写入 `config.ini`。

注意：如果以管理员身份为所有用户安装，`AppDataFolder` 的实际用户上下文可能与最终运行扫描应用的用户有关，后续需要进一步验证多用户场景。

## 3. 设计目标

- 使用 WiX 4.0.4 生成 MSI。
- 同时支持 32 位和 64 位安装包。
- MSI 安装内容与现有 build/install 流程一致。
- CMake 暴露 `msi32` 和 `msi64` target。
- `build.bat msi32` / `build.bat msi64` 可单独生成 MSI。
- `build.bat` 不带参数时仍保留自动构建、安装到 `C:\Windows` 的行为，并额外生成两个 MSI。
- MSI 支持通过 `APP_LANGUAGE` 写入语言配置。
- MSI 安装成功或失败时给用户明确反馈。
- MSI 卸载成功或失败时给用户明确反馈。
- 尽量不依赖 WiX UI 扩展，降低构建环境要求。

非目标：

- 当前不提供完整图形化安装向导。
- 当前不提供 MSI 内置语言选择页面。
- 当前不实现多语言 MSI UI。
- 当前不打包 VC Runtime 或 WiX bootstrapper。
- 当前不处理所有用户的 `%APPDATA%` 配置同步。

## 4. 总体设计

新增 WiX 源文件：

```text
installer\bntech_virtual_scanner.wxs
```

新增安装/卸载结果提示脚本：

```text
installer\install_success.vbs
installer\install_failure.vbs
installer\uninstall_success.vbs
installer\uninstall_failure.vbs
```

CMake 新增自定义 target：

```text
msi32
msi64
```

`build.bat` 新增命令：

```bat
build.bat msi32
build.bat msi64
build.bat msi32 msi64
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
build msi32
build msi64
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

### 5.3 不依赖 WiX UI 扩展

决策：移除 `WixToolset.UI.wixext` 依赖。

原因：

- 实际环境中 WiX UI 扩展解析失败。
- 基础 WiX 4 即可完成打包和安装。
- 降低使用者构建 MSI 的前置条件。

代价：

- MSI 当前没有内置安装向导和语言选择页。
- 语言需要通过命令行 property 传入。

### 5.4 使用 APP_LANGUAGE 控制 config.ini

决策：通过 MSI property `APP_LANGUAGE` 控制语言配置。

原因：

- 与 DS 当前的 config.ini 语言切换设计解耦。
- 默认 `APP_LANGUAGE=en_US` 时不写 config.ini，保持 DS 默认英文行为。
- 非英文时写入 config.ini，满足语言切换需求。

### 5.5 使用 VBScript CustomAction 提示安装/卸载结果

决策：通过 `install_success.vbs`、`install_failure.vbs`、`uninstall_success.vbs` 和 `uninstall_failure.vbs` 弹出消息框。

原因：

- 当前 MSI 不依赖 WiX UI 扩展，仍需要给用户反馈。
- VBScript CustomAction 是 MSI 可用的轻量方式。
- 安装成功和卸载成功提示安排在 `InstallFinalize` 后。
- 安装失败和卸载失败提示安排在 rollback 阶段。

限制：

- 静默安装时弹窗可能不合适。
- 某些企业环境可能限制脚本 CustomAction。

## 6. 架构各层改动

### 6.1 installer 层

新增：

```text
installer\bntech_virtual_scanner.wxs
installer\install_success.vbs
installer\install_failure.vbs
installer\uninstall_success.vbs
installer\uninstall_failure.vbs
```

`bntech_virtual_scanner.wxs` 负责：

- 定义 32/64 位产品名、TWAIN 目录和 UpgradeCode。
- 定义安装文件组件。
- 定义 `%APPDATA%\bntech` 目录。
- 在 `APP_LANGUAGE != en_US` 时写 `config.ini`。
- 定义安装成功/失败、卸载成功/失败提示 CustomAction。

### 6.2 CMake 层

文件：

```text
CMakeLists.txt
```

新增：

- `WIX_EXECUTABLE`
- `WIX_SOURCE_FILE`
- `WIX_INSTALLER_ROOT`
- `msi32` target
- `msi64` target

`msi32` 调用：

```bat
wix build installer\bntech_virtual_scanner.wxs -arch x86 ...
```

`msi64` 调用：

```bat
wix build installer\bntech_virtual_scanner.wxs -arch x64 ...
```

输出到：

```text
build\installer\win32
build\installer\win64
```

### 6.3 build.bat 层

文件：

```text
build.bat
```

新增：

- 参数 `msi32`
- 参数 `msi64`
- `:msi` 子过程

不带参数行为恢复并扩展为：

```text
build both -> install both -> package both MSI
```

单独 MSI：

```bat
build.bat msi32
build.bat msi64
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
  -> 请求管理员权限
  -> 构建 win32
  -> 构建 win64
  -> cmake --install build/win32 --prefix C:/Windows
  -> cmake --install build/win64 --prefix C:/Windows
  -> cmake --build build/win32 --target msi32
  -> cmake --build build/win64 --target msi64
```

### 7.2 单独生成 MSI

```text
build.bat msi32
  -> 配置 build/win32
  -> cmake --build build/win32 --target msi32
```

```text
build.bat msi64
  -> 配置 build/win64
  -> cmake --build build/win64 --target msi64
```

### 7.3 安装语言配置

默认英文：

```bat
msiexec /i bntech_virtual_scanner_win64.msi
```

中文：

```bat
msiexec /i bntech_virtual_scanner_win64.msi APP_LANGUAGE=zh_CN
```

## 8. 局限性

- 当前 MSI 没有内置图形化语言选择页面。
- 当前 MSI 不依赖 WiX UI 扩展，因此安装 UI 很基础。
- `APP_LANGUAGE` 需要通过命令行传入。
- `%APPDATA%` 在管理员安装和多用户环境中的实际落点需要进一步测试。
- 安装/卸载成功失败提示使用 VBScript CustomAction，可能不适合静默安装或受限企业环境。
- 当前失败提示无法精确识别具体失败原因，只能列出常见可能原因。
- 当前没有自动检查 WiX 版本是否为 4.0.4。
- 当前没有把 MSI 构建纳入 CI。

## 9. 下一步工作

- 重新评估是否引入 `WixToolset.UI.wixext`，恢复图形化安装向导和语言选择页面。
- 或自定义最小 MSI UI，实现语言选择而不依赖完整 WixUI。
- 增加静默安装模式下禁用弹窗的条件。
- 增加 WiX 版本检测。
- 增加安装前检查：目标 `.ds` 是否被扫描应用占用。
- 增加安装日志输出说明，例如提示用户使用 `/l*v install.log`。
- 测试管理员安装、多用户登录和不同 `%APPDATA%` 上下文。
- 支持卸载时清理或保留 config.ini 的明确策略。
- 在 CI 中生成并归档 win32/win64 MSI。

</details>

<details>
<summary>English</summary>

## 1. Requirement

The project needs WiX Toolset 4.0.4 based MSI packages for both 32-bit and 64-bit builds.

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

The installer supports language configuration through MSI property `APP_LANGUAGE`. The default is `en_US`. If `APP_LANGUAGE` is not `en_US`, the installer writes `%APPDATA%\bntech\config.ini`.

The MSI also shows message boxes for install success, install rollback failure, uninstall success, and uninstall rollback failure.

## 2. Domain knowledge

A TWAIN Data Source is a DLL-like `.ds` file. 32-bit and 64-bit TWAIN sources are installed in different Windows directories. Because the target is under `C:\Windows`, installation usually requires administrator privileges.

WiX 4 uses `wix build` to generate MSI files from `.wxs` source. This project adds `installer\bntech_virtual_scanner.wxs` and CMake targets `msi32` / `msi64`.

The first UI-based design attempted to use `WixToolset.UI.wixext`, but the extension was not reliably available in the build environment. The current design avoids that dependency.

## 3. Design goals

- Build MSI packages with WiX 4.0.4.
- Support separate 32-bit and 64-bit MSI outputs.
- Install the same files as the existing install flow.
- Provide CMake targets `msi32` and `msi64`.
- Support `build.bat msi32` and `build.bat msi64`.
- Keep no-argument `build.bat` behavior: build, install to `C:\Windows`, and package both MSI files.
- Support language configuration via `APP_LANGUAGE`.
- Show install success/failure feedback to the user.
- Show uninstall success/failure feedback to the user.
- Avoid requiring WiX UI extension.

Non-goals:

- No full graphical install wizard yet.
- No built-in language selection page yet.
- No multilingual MSI UI yet.
- No VC Runtime/bootstrapper packaging yet.
- No all-user AppData synchronization yet.

## 4. Overall design

Added installer files:

```text
installer\bntech_virtual_scanner.wxs
installer\install_success.vbs
installer\install_failure.vbs
installer\uninstall_success.vbs
installer\uninstall_failure.vbs
```

Added CMake custom targets:

```text
msi32
msi64
```

Added build script commands:

```bat
build.bat msi32
build.bat msi64
build.bat msi32 msi64
```

No-argument `build.bat` now performs:

```text
build win32
build win64
install win32
install win64
build msi32
build msi64
```

## 5. Key decisions and rationale

### 5.1 Separate MSI per architecture

Separate MSI packages keep the TWAIN architecture-specific directories simple and avoid mixed-architecture component complexity.

### 5.2 Use CMake build output as WiX source

WiX packages files from `build\win32` or `build\win64`, where CMake already places `.ds`, `FreeImage.dll`, and `TWAIN_logo.png`.

### 5.3 Avoid WiX UI extension

The build environment failed to resolve `WixToolset.UI.wixext`. Removing this dependency makes packaging work with a plain WiX 4 installation.

Trade-off: there is no built-in language selection dialog yet.

### 5.4 Use APP_LANGUAGE for config.ini

The MSI uses `APP_LANGUAGE` to write `config.ini` only when the language is not `en_US`. This keeps the installer loosely coupled with the runtime i18n implementation.

### 5.5 VBScript custom actions for feedback

Install success/failure and uninstall success/failure messages are implemented with `install_success.vbs`, `install_failure.vbs`, `uninstall_success.vbs`, and `uninstall_failure.vbs` so users get visible feedback even without a WiX UI wizard.

## 6. Component changes

### 6.1 Installer layer

`installer\bntech_virtual_scanner.wxs` defines:

- product name and UpgradeCode per architecture;
- TWAIN target directory;
- file components;
- `%APPDATA%\bntech` directory;
- conditional `config.ini` writing;
- install/uninstall success/failure custom actions.

### 6.2 CMake layer

`CMakeLists.txt` adds WiX-related variables and custom targets `msi32` / `msi64`. The targets call `wix build` and write output under `build\installer`.

### 6.3 build.bat layer

`build.bat` adds `msi32`, `msi64`, and a `:msi` helper. The no-argument path builds, installs, and packages both architectures.

### 6.4 i18n config layer

The installer does not duplicate runtime i18n logic. It only writes `%APPDATA%\bntech\config.ini`, which the data source already knows how to read.

## 7. Build and install flow

No-argument flow:

```text
build.bat
  -> elevate for Windows install
  -> build win32 and win64
  -> install both to C:\Windows
  -> build msi32 and msi64
```

Single MSI flow:

```bat
build.bat msi32
build.bat msi64
```

Language examples:

```bat
msiexec /i bntech_virtual_scanner_win64.msi
msiexec /i bntech_virtual_scanner_win64.msi APP_LANGUAGE=zh_CN
```

## 8. Limitations

- No built-in graphical language picker.
- Basic MSI UI because WiX UI extension is not required.
- `APP_LANGUAGE` must be passed from the command line for non-English installs.
- `%APPDATA%` behavior needs more testing under elevated and multi-user installs.
- VBScript custom actions for install/uninstall feedback may be undesirable in silent installs or locked-down enterprise environments.
- Failure messages list likely causes but do not identify the exact root cause.
- WiX 4.0.4 version is not checked automatically.
- MSI build is not yet part of CI.

## 9. Next steps

- Re-evaluate `WixToolset.UI.wixext` or implement a small custom UI for language selection.
- Suppress message boxes during silent installs.
- Add WiX version checks.
- Add pre-install checks for locked `.ds` files.
- Document install logging, for example `/l*v install.log`.
- Test elevated installs and multi-user AppData behavior.
- Define uninstall behavior for config.ini.
- Add CI jobs to build and archive both MSI files.

</details>
