---
layout: page
title: BN Tech Virtual Scanner — User Guide
permalink: /user-guide/
---

End-user usage guide for the BN Tech Virtual Scanner TWAIN 2.5 Data Source.

<details open>
<summary>中文说明</summary>

## 1. 简介

BN Tech Virtual Scanner 是一个 TWAIN 2.5 虚拟平板扫描仪 (Data Source DLL)，
不需要真实硬件，从本地图片目录读取图片，模拟真实扫描仪逐张输出。

适用场景：

- TWAIN 应用开发与回归测试
- 扫描类软件集成、安装与售后演示
- 教学和文档截图
- 自动化 UI 测试（固定输出图片即固定扫描结果）

主要能力：

- 32 位 + 64 位双 TWAIN DS
- 三种传输模式：Native (DIB 句柄)、File (PNG/JPG/BMP/TIFF)、Memory (条带)
- 三种像素格式：彩色 (24-bit RGB)、灰度 (8-bit)、黑白 (1-bit)
- 四种 DPI：150 / 200 / 300 / 600
- 四种页面尺寸：US Letter / US Legal / A4 / A5
- 支持 ShowUI=TRUE 弹出本地网页式设置界面
- 中英文双语界面，由 `config.ini` 控制
- 输出文件携带正确的 DPI 元数据 (PNG/JPG/BMP/TIFF 都已实现)
- 扫描索引持久化，跨进程重启不丢失进度
- MSI 安装包 (32 位 / 64 位)，自动按系统语言选择中英文向导

## 2. 系统要求

- Windows 10 / 11 (x86 或 x64)
- 已安装 TWAIN DSM (大多数 Windows 自带；若没有，可装 [TWAIN Working Group DSM](https://github.com/twain/twain-dsm))
- 默认浏览器 (用于打开本地 settings UI 页面)
- 用户拥有 `%APPDATA%\bntech\` 读写权限

## 3. 安装

### 3.1 使用 MSI 安装包（推荐）

1. 下载与系统位数匹配的安装包：
   - 64 位 Windows: `bntech_virtual_scanner_win64.msi`
   - 32 位 Windows: `bntech_virtual_scanner_win32.msi`
   - 同时安装也是常见做法（一些 32 位应用如 Twack 32 必须使用 32 位 DS，64 位应用如 NAPS2 64 必须使用 64 位 DS）。
2. 双击 MSI；安装向导会自动按系统语言显示中文或英文 UI。
3. 按提示选择安装目录（默认 `C:\Windows\twain_64\bntech\` 或 `C:\Windows\twain_32\bntech\`）。
4. 安装完成后会自动在 Windows TWAIN 目录里注册 `.ds` 文件，TWAIN 应用即可识别。

中文系统会自动在 `%APPDATA%\bntech\config.ini` 写入 `language=zh_CN`，
英文系统默认 `en_US`。如需强制安装某一种语言界面：

```batch
msiexec /i bntech_virtual_scanner_win64.msi TRANSFORMS=:2052   rem 简体中文
msiexec /i bntech_virtual_scanner_win64.msi TRANSFORMS=:1033   rem 英文