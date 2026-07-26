@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion
color 0b

cd /d %~dp0

where g++ >nul 2>&1 || (echo [错误] 未找到 g++, 请先安装 w64devkit 或 MinGW-w64 并加入 PATH & pause & exit /b 1)
where windres >nul 2>&1 || (echo [错误] 未找到 windres, 请确认 MinGW 安装完整 & pause & exit /b 1)

echo [1/3] 清理旧产物...
if exist MockStick-RPG.exe del /q MockStick-RPG.exe
if exist resource.res del /q resource.res

echo [2/3] 编译资源...
windres resource.rc -O coff -o resource.res
if %errorlevel% neq 0 (echo [错误] 资源编译失败 & pause & exit /b 1)

echo [3/3] 开始编译 Pro v5.3...
set IMG_DIR=./externals/imgui
set SOURCES="MockStick-RPG.cpp" %IMG_DIR%/imgui*.cpp %IMG_DIR%/backends/imgui_impl_win32.cpp %IMG_DIR%/backends/imgui_impl_dx11.cpp
set INCS=-I"%IMG_DIR%" -I"%IMG_DIR%/backends"

g++ %SOURCES% resource.res -o MockStick-RPG.exe ^
    %INCS% ^
    -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 ^
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
