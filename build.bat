@echo off
setlocal enabledelayedexpansion

set "VSBASE=C:\Program Files\Microsoft Visual Studio\2022\Professional"
set "VCVARS=%VSBASE%\VC\Auxiliary\Build\vcvarsall.bat"
set "VS_CMAKE=%VSBASE%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA=%VSBASE%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

if /i "%~1"=="install" (
    if "%~2"=="" ("%VS_CMAKE%" --install build/win32 --prefix C:/Windows & "%VS_CMAKE%" --install build/win64 --prefix C:/Windows)
    if /i "%~2"=="win32" "%VS_CMAKE%" --install build/win32 --prefix C:/Windows
    if /i "%~2"=="win64" "%VS_CMAKE%" --install build/win64 --prefix C:/Windows
    goto :eof)

if "%~1"=="" (call :b x86 & call :b x64 & goto :eof)
if /i "%~1"=="win32" (call :b x86 & goto :eof)
if /i "%~1"=="win64" (call :b x64 & goto :eof)
echo Usage: build.bat [win32^|win64^|install [win32^|win64]]
goto :eof

:b
call "%VCVARS%" %~1 >nul 2>&1
if "%~1"=="x86" (set "OUTDIR=win32")
if "%~1"=="x64" (set "OUTDIR=win64")
"%VS_CMAKE%" -S . -B build/!OUTDIR! -G Ninja -DCMAKE_MAKE_PROGRAM="%NINJA%" -DCMAKE_BUILD_TYPE=Release
"%VS_CMAKE%" --build build/!OUTDIR!
exit /b
