# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project follows a practical date-based changelog until formal versioned releases are introduced.

## [Unreleased]

### Added
- i18n / localization support for `en_US` and `zh_CN` UI strings via `%APPDATA%\bntech\config.ini` (`language=zh_CN`).
- `src/localization.h` / `src/localization.cpp`: centralized string table and language detection.
- Language-aware TWAIN identity fields (`TW_IDENTITY` ProductFamily, ProductName, Version.Language, Version.Country).
- Localized web settings UI (`settings_server.cpp`) including folder picker (Unicode `BROWSEINFOW`).
- WiX 4 based MSI installer packages for 32-bit and 64-bit architectures.
- Multi-language MSI with embedded English (en-us) and Simplified Chinese (zh-cn) UI transforms via Windows SDK tools (MsiTran / WiSubStg / WiLangId).
- WixUI_InstallDir standard installation wizard with directory selection and progress.
- CMake targets `msi32` and `msi64` for building MSI packages.
- `build.bat msi32` and `build.bat msi64` commands for standalone MSI generation.
- `add_multilang_msi()` CMake function in `CMakeLists.txt`.
- A web-based settings UI for scan-time options including color mode, DPI, transfer mode, file format, output directory, and output filename.
- File-transfer output support for PNG, JPG, BMP, and TIFF.
- Application-managed file-transfer handling via `DAT_SETUPFILEXFER` / `DAT_IMAGEFILEXFER`.
- Persistent image index tracking through `%APPDATA%\bntech\images\info.json`.
- Support for scanning images from `%APPDATA%\bntech\images` with fallback to `TWAIN_logo.png`.
- DPI metadata writing for scanner-generated output files:
  - PNG `pHYs` chunk.
  - JPG JFIF APP0 density fields.
  - BMP DIB header pixels-per-meter fields.
  - TIFF `XResolution`, `YResolution`, and `ResolutionUnit` tags.
- `ICAP_UNITS = TWUN_INCHES` so TWAIN clients interpret image resolution as DPI/PPI.
- Automatic build-and-install behavior when running `build.bat` without arguments.
- Install-time checks for administrator elevation and locked TWAIN driver files.
- Design documentation: `docs/i18n_design.md`, `docs/msi_installer_design.md`.

### Changed
- Environment requirement now includes WiX Toolset 4.0.4 and Windows SDK (for MsiTran / WiSubStg / WiLangId) for MSI generation.
- `build.bat` without arguments now also generates both 32-bit and 64-bit MSI packages.
- Scan preparation preserves DIB-compatible BGR channel order for native transfer and file output.
- DPI handling so scan-time settings UI choices are propagated to the scanner image data and `TW_IMAGEINFO`.
- Resolution scaling to resize source images according to the requested scan DPI while defaulting missing/screen-like source DPI to 300.
- File saving re-applies DPI metadata immediately before writing output files.
- `build.bat` so explicit architecture builds such as `build.bat win32` and `build.bat win64` still build only and do not install.

### Removed
- Removed VBScript CustomAction files (`install_success.vbs`, `install_failure.vbs`, `uninstall_success.vbs`, `uninstall_failure.vbs`, `language_dialog.vbs`) — replaced by WixUI built-in wizard completion page.

### Fixed
- Fixed missing DPI metadata in files produced through TWAIN file transfer.
- Fixed PNG output showing blank or incorrect Windows Explorer horizontal/vertical resolution.
- Fixed JPG output showing blank Windows Explorer horizontal/vertical resolution.
- Fixed BMP and TIFF output to include horizontal/vertical resolution metadata.
- Fixed XnView "Scan to..." output showing 96 DPI after choosing 600 DPI in the settings UI by ensuring native-transfer image info reports the UI-selected DPI.
- Fixed install failures caused by attempting to overwrite a TWAIN `.ds` file while a scanning application such as XnView still has it loaded, now reporting a clearer message.

### Build
- Builds verified for win32 and win64 Release after the DPI metadata, i18n, and MSI packaging changes.
- MSI output: `build/installer/win32/bntech_virtual_scanner_win32.msi` and `build/installer/win64/bntech_virtual_scanner_win64.msi`.
