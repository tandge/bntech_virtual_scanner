@echo off
setlocal enabledelayedexpansion

set "VSBASE=C:\Program Files\Microsoft Visual Studio\2022\Professional"
set "VCVARS=%VSBASE%\VC\Auxiliary\Build\vcvarsall.bat"
set "VS_CMAKE=%VSBASE%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA=%VSBASE%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

:: Default build type.
set "BUILD_TYPE=Release"

:: Parse options.
:parse_args
if "%~1"=="" goto :dispatch
if /i "%~1"=="clean"     (set "CLEAN=1" & shift & goto :parse_args)
if /i "%~1"=="debug"     (set "BUILD_TYPE=Debug" & shift & goto :parse_args)
if /i "%~1"=="release"   (set "BUILD_TYPE=Release" & shift & goto :parse_args)
if /i "%~1"=="relwithdebinfo" (set "BUILD_TYPE=RelWithDebInfo" & shift & goto :parse_args)
if /i "%~1"=="install"   (set "INSTALL=1" & shift & goto :parse_args)
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
    if "%TARGET%"=="win32" ("%VS_CMAKE%" --install build/win32 --prefix C:/Windows & goto :eof)
    if "%TARGET%"=="win64" ("%VS_CMAKE%" --install build/win64 --prefix C:/Windows & goto :eof)
    "%VS_CMAKE%" --install build/win32 --prefix C:/Windows
    "%VS_CMAKE%" --install build/win64 --prefix C:/Windows
    goto :eof)

:: Build mode.
if "%TARGET%"=="win32" (call :b x86 "%BUILD_TYPE%" & goto :eof)
if "%TARGET%"=="win64" (call :b x64 "%BUILD_TYPE%" & goto :eof)
:: Default: build both.
call :b x86 "%BUILD_TYPE%"
call :b x64 "%BUILD_TYPE%"
goto :eof

:b
call "%VCVARS%" %~1 >nul 2>&1
set "OUTDIR=%~1"
if "%~1"=="x86" (set "OUTDIR=win32")
if "%~1"=="x64" (set "OUTDIR=win64")
echo Building %OUTDIR% (%BUILD_TYPE%)...
"%VS_CMAKE%" -S . -B build/!OUTDIR! -G Ninja ^
    -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE%
if errorlevel 1 exit /b 1
"%VS_CMAKE%" --build build/!OUTDIR!
exit /b
