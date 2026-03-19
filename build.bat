@echo off
setlocal enabledelayedexpansion
color 0b

cd /d %~dp0

echo [1/3] 清理环境...
if exist MockStick-RPG.exe del /q MockStick-RPG.exe
if exist resource.res del /q resource.res

echo [2/3] 编译资源...
windres resource.rc -O coff -o resource.res

echo [3/3] 开始构建 Pro v5.2...
set IMG_DIR=./externals/imgui
set SOURCES="MockStick-RPG.cpp" %IMG_DIR%/imgui*.cpp %IMG_DIR%/backends/imgui_impl_win32.cpp %IMG_DIR%/backends/imgui_impl_dx11.cpp
set INCS=-I"%IMG_DIR%" -I"%IMG_DIR%/backends"

:: 核心修正：添加了 -ldwmapi 和 -ld3dcompiler
g++ %SOURCES% resource.res -o MockStick-RPG.exe ^
    %INCS% ^
    -ld3d11 -ldxgi -ldwmapi -ld3dcompiler -luser32 -lshcore -lgdi32 -ladvapi32 ^
    -static-libgcc -static-libstdc++ ^
    -pthread ^
    -mwindows -O3

if %errorlevel% equ 0 (
    echo ================================
    echo 构建成功: MockStick-RPG.exe
    echo ================================
) else (
    echo 构建失败，请检查报错。
)
pause