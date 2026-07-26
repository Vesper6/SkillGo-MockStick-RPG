#!/usr/bin/env bash
# 使用 MinGW-w64 交叉编译 MockStick-RPG.exe (本地 Linux 或 GitHub Actions 均可运行)
# std::thread 依赖 posix 线程模型, 因此优先使用 -posix 后缀的编译器
set -euo pipefail
cd "$(dirname "$0")/.."

CXX="${CXX:-$(command -v x86_64-w64-mingw32-g++-posix || command -v x86_64-w64-mingw32-g++)}"
WINDRES="${WINDRES:-x86_64-w64-mingw32-windres}"
IMG=externals/imgui

echo "[1/2] 编译资源文件..."
"$WINDRES" resource.rc -O coff -o resource.res

echo "[2/2] 编译 MockStick-RPG.exe (CXX=$CXX)..."
"$CXX" MockStick-RPG.cpp \
    "$IMG"/imgui*.cpp \
    "$IMG"/backends/imgui_impl_win32.cpp \
    "$IMG"/backends/imgui_impl_dx11.cpp \
    resource.res \
    -I"$IMG" -I"$IMG/backends" \
    -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 \
    -ld3d11 -ldxgi -ldwmapi -ld3dcompiler -luser32 -lgdi32 -ladvapi32 -lws2_32 \
    -static -static-libgcc -static-libstdc++ \
    -mwindows -O2 -o MockStick-RPG.exe

echo "构建成功: MockStick-RPG.exe"
