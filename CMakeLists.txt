cmake_minimum_required(VERSION 3.10)

# 项目名称
project("MockStick-RPG")
# 在 project() 之后添加
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
# 设置 C++ 标准为 20 (配合 VS 2026)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. 定义 ImGui 的路径
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/externals/imgui")

# 2. 收集 ImGui 核心源文件
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    # 根据你使用的渲染后端添加（这里以 Win32 + DirectX11 为例，VS 开发最常用）
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

# 3. 将你的源代码和 ImGui 源码一起编译成 exe
add_executable(${PROJECT_NAME} 
    "MockStick-RPG.cpp" 
    "MockStick-RPG.h"
    ${IMGUI_SOURCES}
)

# 4. 告诉编译器去哪里找头文件 (.h)
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# 5. 链接 Windows 系统必要的库 (针对 DX11 后端)
target_link_libraries(${PROJECT_NAME} PRIVATE d3d11 d3dcompiler dwmapi)

# 如果支持，请为 MSVC 编译器启用热重载（保留你原有的配置）
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()