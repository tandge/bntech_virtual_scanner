# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project follows a practical date-based changelog until formal versioned releases are introduced.

## [Unreleased]

### Added
- Added a web-based settings UI for scan-time options including color mode, DPI, transfer mode, file format, output directory, and output filename.
- Added file-transfer output support for PNG, JPG, BMP, and TIFF.
- Added application-managed file-transfer handling via `DAT_SETUPFILEXFER` / `DAT_IMAGEFILEXFER`.
- Added persistent image index tracking through `%APPDATA%\bntech\images\info.json`.
- Added support for scanning images from `%APPDATA%\bntech\images` with fallback to `TWAIN_logo.png`.
- Added DPI metadata writing for scanner-generated output files:
  - PNG `pHYs` chunk.
  - JPG JFIF APP0 density fields.
  - BMP DIB header pixels-per-meter fields.
  - TIFF `XResolution`, `YResolution`, and `ResolutionUnit` tags.
- Added `ICAP_UNITS = TWUN_INCHES` so TWAIN clients interpret image resolution as DPI/PPI.
- Added automatic build-and-install behavior when running `build.bat` without arguments.
- Added install-time checks for administrator elevation and locked TWAIN driver files.

### Changed
- Changed scan preparation to preserve DIB-compatible BGR channel order for native transfer and file output.
- Changed DPI handling so scan-time settings UI choices are propagated to the scanner image data and `TW_IMAGEINFO`.
- Changed resolution scaling to resize source images according to the requested scan DPI while defaulting missing/screen-like source DPI to 300.
- Changed file saving to re-apply DPI metadata immediately before writing output files.
- Changed `build.bat` so explicit architecture builds such as `build.bat win32` and `build.bat win64` still build only and do not install.

### Fixed
- Fixed missing DPI metadata in files produced through TWAIN file transfer.
- Fixed PNG output showing blank or incorrect Windows Explorer horizontal/vertical resolution.
- Fixed JPG output showing blank Windows Explorer horizontal/vertical resolution.
- Fixed BMP and TIFF output to include horizontal/vertical resolution metadata.
- Fixed XnView "Scan to..." output showing 96 DPI after choosing 600 DPI in the settings UI by ensuring native-transfer image info reports the UI-selected DPI.
- Fixed install failures caused by attempting to overwrite a TWAIN `.ds` file while a scanning application such as XnView still has it loaded, now reporting a clearer message.

### Build
- Builds verified for win32 Release after the DPI metadata and native-transfer DPI propagation changes.
