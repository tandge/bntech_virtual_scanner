# I18N / Localization Design

Design notes for adding `zh_CN` and `en_US` localization support to BN Tech Virtual Scanner.

<details open>
<summary>中文说明</summary>

## 1. 需求

项目需要同时支持 `zh_CN` 和 `en_US`。语言通过 `%APPDATA%\bntech\config.ini` 切换，默认是 `en_US`。

本地化范围包括 settings UI 标题、分组标题、字段标签、下拉框选项、Scan / Cancel / Browse 等按钮、本地 HTTP settings UI 的提交确认页面、Windows 文件夹选择对话框标题，以及 TWAIN Data Source 暴露给应用程序的部分身份信息，例如 ProductFamily / ProductName。

配置示例：

```ini
language=zh_CN
```

也支持 `language` / `lang` / `locale` 键名。配置文件不存在、没有语言项或语言值无法识别时，都回退到 `en_US`。

## 2. 领域知识

### 2.1 TWAIN identity

TWAIN DS 通过 `TW_IDENTITY` 向应用暴露设备信息。和语言相关的字段包括 `Version.Language`、`Version.Country`、`ProductFamily`、`ProductName`。

英文使用 `TWLG_ENGLISH` / `TWCY_USA`，名称为 `Virtual Scanner` / `BN Tech Virtual Scanner`。中文使用 `TWLG_CHINESE_PRC` / `TWCY_CHINA`，名称为 `虚拟扫描仪` / `BN Tech 虚拟扫描仪`。

### 2.2 settings UI 架构

settings UI 不是 Win32 Dialog，而是 `SettingsServer` 动态生成 HTML，经本地 HTTP server 由浏览器显示：

```text
TwainDataSource -> SettingsServer -> buildHtmlPage -> browser
```

因此本地化重点是 C++ 中拼接 HTML 和 HTTP 响应的字符串，而不是 `.rc` 对话框资源。

### 2.3 编码

HTML 和 HTTP 响应使用 UTF-8，MSVC 使用 `/utf-8`。Win32 文件夹选择对话框使用 Unicode API，以避免中文标题和路径乱码。

## 3. 设计目标

- 支持 `en_US` 和 `zh_CN`。
- 默认语言固定为 `en_US`。
- 通过 config.ini 运行时切换语言。
- 保持现有 settings UI 架构。
- 集中管理用户可见字符串。
- 中文在浏览器 UI 和文件夹选择对话框中正确显示。

非目标：不自动跟随系统语言；不支持已打开页面实时切换；不引入资源 DLL 或第三方配置解析库；不本地化日志、内部协议字段、文件扩展名、DPI、页面尺寸技术名。

## 4. 总体设计

新增 `src/localization.h` 和 `src/localization.cpp`。模块提供 `Language`、`Strings`、`currentLanguage()`、`strings()`、`configPath()`、`toWide()`。调用方通过 `localization::strings()` 获取当前语言字符串表。

## 5. 主要决策和原因

- 代码内字符串表：当前 UI 是 C++ 动态 HTML，只有两种语言，资源 DLL 过重。
- 默认 `en_US`：符合需求，行为可预测。
- 运行时读取 config.ini：修改配置后重新打开扫描 UI 或重新加载 DS 即可生效。
- UTF-8 + Unicode Win32 API：浏览器适合 UTF-8，Windows 对话框适合 UTF-16。
- 技术标识不翻译：PNG、JPG、DPI、A4、表单字段名等是标准或内部协议。

## 6. 流程图

### 6.1 语言选择流程

```text
需要用户可见文本
  -> localization::strings()
  -> 读取 config.ini
  -> 如果没有 language/lang/locale，默认 en_US
  -> 如果是 zh_CN 兼容值，返回中文字符串表
  -> 否则返回英文字符串表
```

### 6.2 settings UI 流程

```text
TWAIN 应用启用 DS
  -> 如果 ShowUI=true，创建 SettingsServer
  -> buildHtmlPage 获取本地化字符串
  -> 生成 UTF-8 HTML
  -> 浏览器显示页面
  -> 用户 Scan/Cancel
  -> /submit 返回本地化确认页面
```

### 6.3 Browse 流程

```text
点击 Browse
  -> /browse 请求
  -> 获取本地化标题
  -> UTF-8 转 UTF-16
  -> SHBrowseForFolderW
  -> 获取 UTF-16 路径
  -> 转 UTF-8 返回浏览器
```

## 7. 架构组件改动

### 7.1 CMakeLists.txt

将 `src/localization.cpp` 加入构建。

### 7.2 localization 模块

新增语言枚举、字符串表、配置解析、`language` / `lang` / `locale` 键名兼容、UTF-8 BOM 处理、语言值归一化、UTF-8 到 UTF-16 转换。

### 7.3 settings_server.cpp

`buildHtmlPage()` 使用 `localization::strings()`；本地化页面标题、分组标题、标签、按钮、提示文字；`/submit` 确认页面本地化；`/browse` 标题本地化；文件夹选择从 ANSI API 改为 Unicode API；`/browse` 返回 `text/plain; charset=utf-8`。

### 7.4 twain_data_source.cpp

`initialize()` 根据语言调整 `identity_`。中文时设置 `TWLG_CHINESE_PRC` / `TWCY_CHINA`，并复制本地化 ProductFamily / ProductName。

### 7.5 README.md

增加中英文语言切换说明。

## 8. 端到端数据流

```text
TWAIN 应用请求 ShowUI
  -> TwainDataSource 创建 SettingsServer
  -> SettingsServer 构建 HTML
  -> localization 读取 config.ini
  -> 返回 en_US 或 zh_CN 字符串表
  -> 浏览器显示对应语言页面
  -> 用户提交
  -> DS 继续扫描流程
```

## 9. 测试建议

- 删除 config.ini，确认默认英文。
- 写入 `language=zh_CN`，确认 settings UI、按钮、Browse 对话框、确认页显示中文。
- 写入 `language=en_US`，确认切回英文。
- 写入非法值，确认回退英文且不报错。
- 测试包含中文路径的输出目录。
- 修改 config.ini 后关闭并重新打开扫描应用，避免 DLL 缓存影响。

## 10. 风险

- TWAIN ProductName/ProductFamily 是窄字符数组，老 TWAIN 应用可能按 ANSI 解释 UTF-8 中文导致乱码。
- HTML 对 output directory / filename 尚未完整转义，未来需要 `htmlEscape()`。
- 当前每次获取字符串都会读取 config.ini，未来高频请求可加缓存。
- 中文依赖源码 UTF-8 和 MSVC `/utf-8`。
- 浏览器可能自动翻译页面。

## 11. 当前限制

- 仅支持 `en_US` 和 `zh_CN`。
- 不自动检测系统语言。
- 不支持页面打开后的实时语言切换。
- 没有 UI 内语言选择器。
- 不本地化日志和技术标识。
- 没有外部翻译文件，改翻译需要重新编译。

## 12. 未来改进

- 增加 `htmlEscape()`。
- 给 HTML 增加 `lang="en-US"` / `lang="zh-CN"`。
- 增加 UI 内语言选择器并写回 config.ini。
- 增加配置缓存和文件修改时间检测。
- 支持更多语言，例如 `zh_TW`、`ja_JP`。
- 将字符串表外置到资源文件。
- 优化 TWAIN identity 的编码兼容策略。
- 增加自动化测试覆盖默认英文、中文、非法配置回退、UTF-8 BOM 和中文路径。

</details>

<details>
<summary>English</summary>

## 1. Requirement

The project must support `zh_CN` and `en_US`. Localized content includes settings UI titles, labels, dropdown options, buttons, confirmation pages, the folder picker title, and selected TWAIN identity strings. The language is selected by `%APPDATA%\bntech\config.ini`. Default is `en_US`.

## 2. Domain knowledge

A TWAIN DS exposes device information through `TW_IDENTITY`. Language-related fields include `Version.Language`, `Version.Country`, `ProductFamily`, and `ProductName`. The settings UI is generated HTML served by `SettingsServer`, so localization focuses on C++ strings used for HTML and HTTP responses. HTML uses UTF-8, while Windows folder picker APIs use UTF-16.

## 3. Design goals

- Support `en_US` and `zh_CN`.
- Default to `en_US`.
- Select language at runtime through config.ini.
- Keep the existing settings UI architecture.
- Centralize user-visible strings.
- Display Chinese correctly in both browser UI and folder picker.

Non-goals: no automatic system-language detection, no live switching for already opened pages, no resource DLL, no third-party parser, and no localization for logs or technical identifiers.

## 4. Overall design

Add `src/localization.h` and `src/localization.cpp`. The module provides language enums, string tables, config parsing, current-language selection, and UTF-8 to UTF-16 conversion. Callers use `localization::strings()`.

## 5. Key decisions and rationale

- In-code string tables: simplest fit for C++ generated HTML and two languages.
- Fixed `en_US` default: matches the requirement and keeps tests predictable.
- Runtime config read: changes apply after reopening the UI or reloading the DS.
- UTF-8 HTML plus Unicode Win32 APIs: browser handles UTF-8 and Windows dialogs handle UTF-16 reliably.
- Technical identifiers are not translated: names such as PNG, DPI, A4, and form fields are standards or internal protocol details.

## 6. Flowcharts

### 6.1 Language selection

```text
Need UI text
  -> localization::strings()
  -> Read config.ini
  -> If no language/lang/locale, default to en_US
  -> If zh_CN compatible, return Chinese strings
  -> Otherwise return English strings
```

### 6.2 Settings UI

```text
TWAIN app enables DS
  -> If ShowUI=true, create SettingsServer
  -> buildHtmlPage gets localized strings
  -> Generate UTF-8 HTML
  -> Browser shows page
  -> User clicks Scan/Cancel
  -> /submit returns localized confirmation
```

## 7. Component changes

- `CMakeLists.txt`: add `src/localization.cpp`.
- localization module: add language enum, string tables, config parser, aliases, BOM handling, normalization, and UTF conversion.
- `settings_server.cpp`: localize generated HTML, `/submit`, and `/browse`; switch folder picker to Unicode APIs.
- `twain_data_source.cpp`: set localized TWAIN language/country and ProductFamily/ProductName.
- `README.md`: document language switching.

## 8. End-to-end data flow

```text
TWAIN ShowUI request
  -> TwainDataSource creates SettingsServer
  -> SettingsServer builds HTML
  -> localization reads config.ini
  -> en_US or zh_CN string table is selected
  -> browser displays localized UI
  -> user submits
  -> DS continues scanning
```

## 9. Test plan

- Delete config.ini and verify English default.
- Set `language=zh_CN` and verify Chinese UI, buttons, Browse dialog, and confirmation page.
- Set `language=en_US` and verify English.
- Set an invalid value and verify fallback to English.
- Test output directories containing Chinese characters.
- Close and reopen scanning applications after config changes to avoid DLL caching.

## 10. Risks

- TWAIN ProductName/ProductFamily are narrow character arrays; old TWAIN apps may interpret UTF-8 as ANSI and show garbled Chinese.
- HTML does not fully escape user-provided output directory and filename values yet.
- Config is read for each string lookup; caching may be needed later.
- Chinese depends on UTF-8 source files and `/utf-8`.
- Browser auto-translation may alter visible text.

## 11. Current limitations

Only `en_US` and `zh_CN` are supported. There is no system-language detection, live switching, UI language selector, log localization, or external translation file.

## 12. Future improvements

Add `htmlEscape()`, HTML `lang` attribute, an in-UI language selector, config caching, more languages, external translation resources, TWAIN identity encoding improvements, and automated i18n tests.

</details>
