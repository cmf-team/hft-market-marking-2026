@echo off
setlocal enabledelayedexpansion

REM Build script for CMF hft-market-making-2026 under Visual Studio 2026 / MSVC x64.
REM Put this file anywhere, or keep it at: S:\YandexDisk\CMF\Cppuild-hft-market-making-2026.bat

set "PROJECT_DIR=S:\YandexDisk\CMF\Cpp\hft-market-making-2026"
set "VS_VCVARS=D:\Program Files\Microsoft Visual Studio8\Enterprise\VC\Auxiliary\Buildcvarsall.bat"
set "GENERATOR=Visual Studio 18 2026"
set "PLATFORM=x64"
set "CONFIG=Debug"

if not exist "%VS_VCVARS%" (
    echo ERROR: Visual Studio vcvarsall.bat not found:
    echo   "%VS_VCVARS%"
    echo Edit VS_VCVARS in this .bat if your VS 2026 is installed elsewhere.
    pause
    exit /b 1
)

call "%VS_VCVARS%" amd64
if errorlevel 1 exit /b 1

cd /d "%PROJECT_DIR%"
if errorlevel 1 (
    echo ERROR: Cannot cd to "%PROJECT_DIR%"
    pause
    exit /b 1
)

if exist build rmdir /s /q build
if exist .vs rmdir /s /q .vs

cmake -S . -B build -G "%GENERATOR%" -A %PLATFORM% -DBUILD_TESTS=OFF
if errorlevel 1 (
    echo ERROR: CMake configure failed.
    pause
    exit /b 1
)

cmake --build build --config %CONFIG%
if errorlevel 1 (
    echo ERROR: CMake build failed.
    pause
    exit /b 1
)

echo.
echo SUCCESS.
echo Solution: "%PROJECT_DIR%uild\hft-market-making.sln"
echo Executable: "%PROJECT_DIR%uildin\%CONFIG%\hft-market-making.exe"
echo.
pause
