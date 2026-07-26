# SkillGo-MockStick-RPG `v5.3-Stable`

[![Build](https://github.com/Vesper6/SkillGo-MockStick-RPG/actions/workflows/build.yml/badge.svg)](https://github.com/Vesper6/SkillGo-MockStick-RPG/actions/workflows/build.yml)

**SkillGo-MockStick-RPG** 是一款基于 C++ 和 Dear ImGui 开发的高性能 Android 远程控制与自动化映射工具。它通过集成 ADB (Android Debug Bridge) 与 Scrcpy 协议，实现了极低延迟的屏幕同步，并为移动端 RPG 游戏及自动化测试提供了物理键盘（WASD）映射与宏指令录制功能。


## 🚀 核心特性

  * **⚡ 零延迟同步**：基于 Scrcpy 核心算法，实现毫秒级的实时画面传输。
  * **⌨️ 物理键盘映射**：原生支持物理键盘 `W/A/S/D` 映射至手机虚拟摇杆，支持**对角线组合键**（如 `W+D` 斜向移动）。
  * **🎛️ 参数面板可调**：摇杆中心坐标、滑动步长/时长、手机分辨率均可在 UI 中调整，并自动持久化到 `mockstick_config.ini`。
  * **📼 宏指令引擎**：内置指令录制器，可精确记录用户的滑动、点击及延迟，支持无限循环或**指定次数**执行脚本；短距离操作自动识别为点按（`input tap`）。
  * **🛠️ 开发者面板**：实时监测 ADB 连接状态，支持多设备序列号切换及 Shell 指令直达。
  * **📦 绿色便携设计**：采用静态链接编译，无须安装冗重的运行库，解压即用。
  * **🤖 CI 自动构建**：每次推送均由 GitHub Actions 交叉编译验证，打 `v*` 标签自动发布 Release。


## 🛠️ 技术架构

本工具采用模块化设计，确保各组件的高效协作：

  * **GUI 层**：使用 [Dear ImGui](https://github.com/ocornut/imgui) 构建，采用 DirectX 11 硬件加速渲染（无独显环境自动回退 WARP 软件渲染）。
  * **底层通信**：通过管道（Pipe）技术与 `adb shell` 进行高速异步通信。
  * **画面捕获**：利用 `PrintWindow` 接口拦截 Scrcpy 渲染句柄，实现 UI 内部的实时镜像嵌入。
  * **多线程模型**：UI 渲染线程与脚本执行线程分离（互斥锁保护共享脚本数据），确保宏指令运行期间界面不卡顿。


## 📥 快速开始

### 环境依赖

  * **Windows 10/11** (x64)
  * **Android 设备**：需开启“开发者选项”中的“USB 调试”和“USB 调试（安全设置）”。

### 编译安装

**方式一：Windows 本机构建 (MinGW-w64 / w64devkit)**

```batch
git clone https://github.com/Vesper6/SkillGo-MockStick-RPG.git
cd SkillGo-MockStick-RPG
build.bat
```

**方式二：Linux / CI 交叉编译**

```bash
sudo apt-get install -y g++-mingw-w64-x86-64 binutils-mingw-w64-x86-64
./ci/build-mingw.sh
```

**方式三：直接下载**

无需自行编译，前往 [Releases](https://github.com/Vesper6/SkillGo-MockStick-RPG/releases) 或任意一次 [Actions](https://github.com/Vesper6/SkillGo-MockStick-RPG/actions) 构建下载 `MockStick-RPG.exe`。

### 运行说明

将生成的 `MockStick-RPG.exe` 放入包含以下依赖的文件夹中：

  * `adb.exe`
  * `scrcpy.exe` 及其关联的 `.dll` 文件


## 📖 使用指南

1.  **连接设备**：点击“刷新设备列表”，确认序列号正确后点击“连接镜像窗口”。
2.  **摇杆设置**：在控制面板“摇杆与设备参数”一节中调整摇杆中心 X/Y 与滑动步长，使其对准游戏内的虚拟摇杆中心，点击“保存参数配置”即可持久化。
3.  **键盘操控**：保持软件窗口置顶，即可通过键盘 `W/A/S/D`（含组合键斜向）控制人物移动。
4.  **录制宏**：
      * 点击“开始录制”，在镜像窗口或手机上进行操作。
      * 点击“停止录制”并保存为 `.txt`。
      * 设置循环次数（0 为无限），点击“启动循环脚本”开启自动化流程；“停止脚本”即时生效。


## 📂 目录结构

```text
.
├── .github/workflows/  # CI 自动构建与发布
├── ci/                 # 交叉编译脚本 (本地与 CI 共用)
├── externals/          # 第三方库 (Dear ImGui)
├── MockStick-RPG.cpp   # 核心源代码
├── resource.rc         # 资源定义 (图标/元数据)
├── build.bat           # Windows 本机构建脚本
├── .gitignore          # Git 忽略规则
└── README.md           # 项目文档
```


## 🛡️ 开源协议

本项目基于 [MIT License](LICENSE) 协议开源。


## 👨‍💻 作者

**Vesper** - [GitHub @Vesper6](https://github.com/Vesper6)
