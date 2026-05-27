---
layout: page
title: BN Tech Virtual Scanner — FAQ
permalink: /faq/
---

Frequently asked questions for end users of the BN Tech Virtual Scanner TWAIN Data Source.

<details open>
<summary>中文说明</summary>

## 常见问题

### Q1. TWAIN 应用列表里看不到 BN Tech Virtual Scanner

- 32 位应用必须装 32 位 DS；64 位应用必须装 64 位 DS。
- 确认 `C:\Windows\twain_32\bntech\bntech_virtual_scanner.ds` 或 `C:\Windows\twain_64\bntech\bntech_virtual_scanner.ds` 存在。
- 部分应用缓存了源列表，重启应用即可。

### Q2. 扫描出来颜色不正确（红蓝互换）

- 此问题已在 Memory Transfer 模式中修正：确认你使用的是最新版本 DLL；若仍发生请提交 issue。

### Q3. settings UI 没弹出

- 应用可能用 `ShowUI=FALSE` 直接扫描，这是正常行为。
- 检查默认浏览器是否能正常启动；DS 用本机 HTTP 端口提供 UI。
- 确认 `TWAIN_logo.png` / `FreeImage.dll` 与 `.ds` 同目录，UI 进程缺组件会启动失败。

### Q4. 扫描结果一直是同一张图

- DS 按字母序自动前进。若只有一张图，自然每次都一样。
- 删除 `%APPDATA%\bntech\images\info.json` 或加更多图片。

### Q5. 安装报"文件正在被使用"

- 关闭所有正在使用扫描 DS 的应用（XnView / Twack 32 / NAPS2 / Photoshop 等）后重试。
- TWAIN DS 本质是 DLL，被应用 LoadLibrary 后无法覆盖。

### Q6. 想强制切换 UI 语言

- 编辑 `%APPDATA%\bntech\config.ini`，设置 `language=en_US` 或 `language=zh_CN`，然后重新启动应用。

### Q7. 大图扫描应用崩溃

- A4 600 DPI 24-bit 单图约 100 MB，加上 DIB 副本可达 200 MB。32 位应用受 2 GB 地址空间限制，过大图时请改用 300 DPI 或 Gray。

### Q8. 想验证 32 位 / 64 位是否都装好

- 64 位测试：用 IrfanView 64 (官网下载) 或 NAPS2 64 → File → Acquire → 看是否有源。
- 32 位测试：用 Twack 32 / XnView (32 位) → 同样路径。

## 反馈

更多使用说明见 [用户使用手册](user-guide.md)；详细设计见 [开发技术](devlog.md)。
如以上 FAQ 仍无法解决问题，请在项目仓库提交 issue，并附上 Windows 版本、宿主应用名 + 版本、DS 位数、settings UI 截图或 `%APPDATA%\bntech\` 下相关文件。
邮箱：tandge@gmail.com

</details>

<details>
<summary>English</summary>

## FAQ

### Q1. The TWAIN source list does not show BN Tech Virtual Scanner

- 32-bit apps require the 32-bit DS; 64-bit apps require the 64-bit DS.
- Verify `C:\Windows\twain_32\bntech\bntech_virtual_scanner.ds` (or the 64-bit equivalent) exists.
- Some apps cache the source list; restart the app.

### Q2. Scan output has swapped red/blue channels

- This was fixed for Memory Transfer mode. Make sure you are running the latest DLL; file an issue if it still happens.

### Q3. The settings UI does not appear

- The host app may have started with `ShowUI=FALSE`; this is normal.
- Verify the default browser launches; the DS serves the UI on a local HTTP port.
- Make sure `TWAIN_logo.png` and `FreeImage.dll` are in the same folder as the `.ds` — missing components prevent UI startup.

### Q4. Every scan returns the same image

- The DS auto-advances alphabetically. With only one image the result repeats.
- Delete `%APPDATA%\bntech\images\info.json` or add more images.

### Q5. Install says "file in use"

- Close every app that has loaded the DS (XnView / Twack 32 / NAPS2 / Photoshop, …) and retry.
- A TWAIN DS is a DLL; it cannot be overwritten while loaded.

### Q6. How do I force a UI language?

- Edit `%APPDATA%\bntech\config.ini` to set `language=en_US` or `language=zh_CN`, then restart the host app.

### Q7. Host app crashes on large scans

- A4 at 600 DPI 24-bit is ~100 MB per image, plus the DIB copy. 32-bit apps hit the 2 GB address-space wall; drop to 300 DPI or use Gray for very large scans.

### Q8. How do I verify both 32-bit and 64-bit work?

- 64-bit: use IrfanView 64 or NAPS2 64 → File → Acquire → check the source list.
- 32-bit: use Twack 32 or XnView (32-bit) → same path.

## Feedback

For full usage instructions see the [User Guide](user-guide.md); for design details see [Dev Logs](devlog.md).
If the FAQ above does not resolve your issue, please file an issue on the project repository and include: Windows version, host app name + version, DS bitness (32 / 64), a settings UI screenshot, or files under `%APPDATA%\bntech\`.
Email: tandge@gmail.com

</details>