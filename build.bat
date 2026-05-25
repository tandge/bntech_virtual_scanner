@echo off
cls
setlocal enabledelayedexpansion
set "SCRIPT_PATH=%~f0"
set "START_DIR=%CD%"
set "ORIGINAL_ARGS=%*"

set "VSBASE=C:\Program Files\Microsoft Visual Studio\2022\Professional"
set "VCVARS=%VSBASE%\VC\Auxiliary\Build\vcvarsall.bat"
set "VS_CMAKE=%VSBASE%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA=%VSBASE%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
set "WIX_UI_EXTENSION=WixToolset.UI.wixext"

:: Default build type.
set "BUILD_TYPE=Release"

:: With no arguments, build both architectures, install them, and create both
:: MSI packages.
if "%~1"=="" (
    set "INSTALL_AFTER_BUILD=1"
    set "MSI32=1"
    set "MSI64=1"
)

:: Install to C:\Windows requires administrator rights.  When a no-argument
:: build will auto-install, relaunch this script elevated before building so
:: the install step does not fail after the build completes.
if defined INSTALL_AFTER_BUILD call :ensure_admin || exit /b 1

:: Parse options.
:parse_args
if "%~1"=="" goto :dispatch
if /i "%~1"=="clean"     (set "CLEAN=1" & shift & goto :parse_args)
if /i "%~1"=="debug"     (set "BUILD_TYPE=Debug" & shift & goto :parse_args)
if /i "%~1"=="release"   (set "BUILD_TYPE=Release" & shift & goto :parse_args)
if /i "%~1"=="relwithdebinfo" (set "BUILD_TYPE=RelWithDebInfo" & shift & goto :parse_args)
if /i "%~1"=="install"   (set "INSTALL=1" & shift & goto :parse_args)
if /i "%~1"=="msi32"     (set "MSI32=1" & shift & goto :parse_args)
if /i "%~1"=="msi64"     (set "MSI64=1" & shift & goto :parse_args)
if /i "%~1"=="win32"    (set "TARGET=win32" & shift & goto :parse_args)
if /i "%~1"=="win64"    (set "TARGET=win64" & shift & goto :parse_args)
shift & goto :parse_args

:dispatch
:: Clean mode: remove build directories.
if defined CLEAN (
    if exist build\win32 rmdir /s /q build\win32
    if exist build\win64 rmdir /s /q build\win64
    echo Build directories cleaned.
    goto :eof)

:: Install mode.
if defined INSTALL (
    call :ensure_admin || exit /b 1
    if "%TARGET%"=="win32" (call :install_one win32 & goto :eof)
    if "%TARGET%"=="win64" (call :install_one win64 & goto :eof)
    call :install_one win32 || exit /b 1
    call :install_one win64 || exit /b 1
    goto :eof)

:: MSI package mode. Examples:
::   build.bat msi32
::   build.bat msi64
::   build.bat msi32 msi64
:: No-argument builds also install, so let them continue to the default build
:: block and create MSI packages after installation.
if not defined INSTALL_AFTER_BUILD (
    if defined MSI32 (
        call :msi x86 msi32 || exit /b 1
    )
    if defined MSI64 (
        call :msi x64 msi64 || exit /b 1
    )
    if defined MSI32 goto :eof
    if defined MSI64 goto :eof
)

:: Build mode.
if "%TARGET%"=="win32" (
    call :b x86 "%BUILD_TYPE%" || exit /b 1
    if defined INSTALL_AFTER_BUILD call :install_one win32 || exit /b 1
    goto :eof)
if "%TARGET%"=="win64" (
    call :b x64 "%BUILD_TYPE%" || exit /b 1
    if defined INSTALL_AFTER_BUILD call :install_one win64 || exit /b 1
    goto :eof)
:: Default: build both.  With no arguments, also install both.
call :b x86 "%BUILD_TYPE%" || exit /b 1
call :b x64 "%BUILD_TYPE%" || exit /b 1
if defined INSTALL_AFTER_BUILD (
    call :install_one win32 || exit /b 1
    call :install_one win64 || exit /b 1
    if defined MSI32 call :msi x86 msi32 || exit /b 1
    if defined MSI64 call :msi x64 msi64 || exit /b 1
)
goto :eof

:b
call "%VCVARS%" %~1 >nul 2>&1
set "OUTDIR=%~1"
if "%~1"=="x86" (set "OUTDIR=win32")
if "%~1"=="x64" (set "OUTDIR=win64")
echo Building %OUTDIR% (%BUILD_TYPE%)...
"%VS_CMAKE%" -S . -B build/!OUTDIR! -G Ninja ^
    -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -DWIX_UI_EXTENSION="%WIX_UI_EXTENSION%"
if errorlevel 1 exit /b 1
"%VS_CMAKE%" --build build/!OUTDIR!
exit /b

:msi
call :ensure_wix_ui_extension || exit /b 1
call "%VCVARS%" %~1 >nul 2>&1
set "OUTDIR=%~1"
if "%~1"=="x86" (set "OUTDIR=win32")
if "%~1"=="x64" (set "OUTDIR=win64")
echo Building !OUTDIR! MSI (%BUILD_TYPE%)...
"%VS_CMAKE%" -S . -B build/!OUTDIR! -G Ninja ^
    -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -DWIX_UI_EXTENSION="%WIX_UI_EXTENSION%"
if errorlevel 1 exit /b 1
"%VS_CMAKE%" --build build/!OUTDIR! --target %~2
exit /b

:install_one
set "INSTALL_ARCH=%~1"
if /i "%INSTALL_ARCH%"=="win32" set "INSTALL_DS=%SystemRoot%\twain_32\bntech\bntech_virtual_scanner.ds"
if /i "%INSTALL_ARCH%"=="win64" set "INSTALL_DS=%SystemRoot%\twain_64\bntech\bntech_virtual_scanner.ds"
if defined INSTALL_DS call :check_not_locked "%INSTALL_DS%" || exit /b 1
"%VS_CMAKE%" --install build/%~1 --prefix C:/Windows
exit /b

:check_not_locked
if not exist "%~1" exit /b 0
powershell -NoProfile -ExecutionPolicy Bypass -Command "try { $f=[System.IO.File]::Open('%~1','Open','ReadWrite','None'); $f.Close(); exit 0 } catch { exit 1 }" >nul 2>&1
if not errorlevel 1 exit /b 0
echo.
echo Cannot install: "%~1" is currently in use.
echo Please close XnView / scanning applications that may have loaded the TWAIN driver,
echo then run build.bat again.
echo.
exit /b 1

:ensure_wix_ui_extension
set "WIX_UI_EXTENSION=WixToolset.UI.wixext"
for /f "usebackq delims=" %%F in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$roots=@($env:USERPROFILE, $env:ProgramData) | Where-Object { $_ }; foreach($r in $roots){ Get-ChildItem -Path $r -Filter WixToolset.UI.wixext.dll -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName }"`) do (
    set "WIX_UI_EXTENSION=%%F"
    exit /b 0
)

wix extension list 2>nul | findstr /i "WixToolset.UI.wixext" >nul
if not errorlevel 1 exit /b 0

echo WiX UI extension WixToolset.UI.wixext is not installed. Installing it...
wix extension add WixToolset.UI.wixext
if errorlevel 1 (
    echo.
    echo Failed to install WixToolset.UI.wixext.
    echo Please install it manually with:
    echo   wix extension add WixToolset.UI.wixext
    echo.
    exit /b 1
)

for /f "usebackq delims=" %%F in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$roots=@($env:USERPROFILE, $env:ProgramData) | Where-Object { $_ }; foreach($r in $roots){ Get-ChildItem -Path $r -Filter WixToolset.UI.wixext.dll -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName }"`) do (
    set "WIX_UI_EXTENSION=%%F"
    exit /b 0
)
exit /b 0

:ensure_admin
net session >nul 2>&1
if not errorlevel 1 exit /b 0

echo Administrator rights are required to install to C:\Windows.
echo Requesting elevation...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%COMSPEC%' -ArgumentList '/c cd /d \"%START_DIR%\" && \"%SCRIPT_PATH%\" %ORIGINAL_ARGS%' -Verb RunAs -Wait"
exit /b %ERRORLEVEL%
