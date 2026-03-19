# SkillGo-MockStick-RPG `v5.2-Stable`

[](https://isocpp.org/)
[](https://www.microsoft.com/windows)
[](https://www.google.com/search?q=LICENSE)

**SkillGo-MockStick-RPG** 是一款基于 C++ 和 Dear ImGui 开发的高性能 Android 远程控制与自动化映射工具。它通过集成 ADB (Android Debug Bridge) 与 Scrcpy 协议，实现了极低延迟的屏幕同步，并为移动端 RPG 游戏及自动化测试提供了物理键盘（WASD）映射与宏指令录制功能。


## 🚀 核心特性

  * **⚡ 零延迟同步**：基于 Scrcpy 核心算法，实现毫秒级的实时画面传输。
  * **⌨️ 物理键盘映射**：原生支持物理键盘 `W/A/S/D` 映射至手机虚拟摇杆，支持自定义偏移量与滑动步长。
  * **📼 宏指令引擎**：内置指令录制器，可精确记录用户的滑动、点击及延迟，支持无限循环执行脚本。
  * **🛠️ 开发者面板**：实时监测 ADB 连接状态，支持多设备序列号切换及 Shell 指令直达。
  * **📦 绿色便携设计**：采用静态链接编译，无须安装冗重的运行库，解压即用。



## 🛠️ 技术架构

本工具采用模块化设计，确保各组件的高效协作：

  * **GUI 层**：使用 [Dear ImGui](https://github.com/ocornut/imgui) 构建，采用 DirectX 11 硬件加速渲染。
  * **底层通信**：通过管道（Pipe）技术与 `adb shell` 进行高速异步通信。
  * **画面捕获**：利用 `PrintWindow` 接口拦截 Scrcpy 渲染句柄，实现 UI 内部的实时镜像嵌入。
  * **多线程模型**：UI 渲染线程与脚本执行线程分离，确保宏指令运行期间界面不卡顿。



## 📥 快速开始

### 环境依赖

  * **Windows 10/11** (x64)
  * **Android 设备**：需开启“开发者选项”中的“USB 调试”和“USB 调试（安全设置）”。

### 编译安装 (MinGW-w64)

如果你希望自行从源码构建，请确保已安装 `w64devkit` 或类似的 MinGW 环境：

1.  克隆仓库：
    ```bash
    git clone https://github.com/Vesper6/SkillGo-MockStick-RPG.git
    cd SkillGo-MockStick-RPG
    ```
2.  运行打包脚本：
    ```batch
    ./build.bat
    ```

### 运行说明

将生成的 `MockStick-RPG.exe` 放入包含以下依赖的文件夹中：

  * `adb.exe`
  * `scrcpy.exe` 及其关联的 `.dll` 文件



## 📖 使用指南

1.  **连接设备**：点击“刷新设备列表”，确认序列号正确后点击“连接镜像窗口”。
2.  **摇杆设置**：在控制面板中调整 `g_JoyX` 和 `g_JoyY`，使其对准游戏内的虚拟摇杆中心。
3.  **键盘操控**：保持软件窗口置顶，即可通过键盘 `W/A/S/D` 控制人物移动。
4.  **录制宏**：
      * 点击“开始录制”，在镜像窗口或手机上进行操作。
      * 点击“停止录制”并保存为 `.txt`。
      * 点击“执行循环脚本”开启自动化流程。


## 📂 目录结构

```text
.
├── externals/          # 第三方库 (Dear ImGui)
├── MockStick-RPG.cpp   # 核心源代码
├── resource.rc         # 资源定义 (图标/元数据)
├── build.bat           # 自动化构建脚本
├── .gitignore          # Git 忽略规则
└── README.md           # 项目文档
```



## 🛡️ 开源协议

本项目基于 [MIT License](https://www.google.com/search?q=LICENSE) 协议开源。


## 👨‍💻 作者

**Vesper** - [GitHub @Vesper6](https://www.google.com/search?q=https://github.com/Vesper6)

