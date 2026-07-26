# SkillGo-MockStick-RPG `v6.0-Stable`

[![Build](https://github.com/Vesper6/SkillGo-MockStick-RPG/actions/workflows/build.yml/badge.svg)](https://github.com/Vesper6/SkillGo-MockStick-RPG/actions/workflows/build.yml)

**SkillGo-MockStick-RPG** 是一款基于 C++ 和 Dear ImGui 开发的高性能 Android 远程控制与自动化平台。它通过集成 ADB (Android Debug Bridge) 与 Scrcpy 协议实现极低延迟的屏幕同步，内置条件宏引擎、找图/OCR 识别、多设备广播、定时调度，并可通过 **HTTP API + MCP** 让 Claude 等 AI 助手直接"看屏操作"你的手机。


## 🚀 核心特性

### 操控
  * **⚡ 零延迟同步**：基于 Scrcpy 核心算法，实现毫秒级的实时画面传输。
  * **⌨️ 物理键盘映射**：`W/A/S/D` 映射至虚拟摇杆，支持**对角线组合键**；摇杆中心/步长/时长均可在 UI 调整并持久化。
  * **📱 多设备并行**：勾选多台设备后指令同步广播，一次操作控制整支"搬砖车队"。

### 自动化
  * **📼 条件宏引擎 v2**：除录制回放外，支持文本 DSL 编写智能脚本——`wait_pixel` 等像素变色、`if_pixel` 条件跳转、`goto` 循环、`random_wait` 随机延迟、`click_image` 找图点击、`wait_text` OCR 文字触发。
  * **🔍 找图 + OCR**：内置模板匹配引擎（粗到细搜索）与 Windows 原生 OCR 集成，界面弹窗、加载时长变化不再打乱流程。
  * **⏰ 定时调度**：到点自动唤醒屏幕执行指定脚本（每日签到、体力清理无人值守）。
  * **📝 可视化编辑器**：录制的每一步可在表格中单独修改坐标/延迟、插入、删除、排序、单步试运行。

### AI 接入
  * **🤖 MCP Server**：附带零依赖的 MCP 服务器 (`mcp/mockstick_mcp.py`)，Claude 等 AI 可直接截屏观察、读取 UI 控件树、点击滑动、执行宏——对 AI 说"帮我做完每日任务"即可。
  * **🌐 HTTP REST API**：本机 `127.0.0.1` 接口（`/tap` `/swipe` `/screenshot` `/uidump` `/ocr` `/macro/run` 等），Python/快捷指令/任何语言均可编排。

### 工程
  * **🗂️ 配置档案**：按游戏保存参数+定时配置，一键切换。
  * **📋 运行日志**：所有操作/脚本执行/异常记录到 `logs/`，UI 内实时查看。
  * **📦 绿色便携**：静态链接编译，解压即用；**CI 自动构建**，打 `v*` 标签自动发布 Release。


## 🛠️ 技术架构

  * **GUI 层**：[Dear ImGui](https://github.com/ocornut/imgui) + DirectX 11 硬件加速（无独显自动回退 WARP 软件渲染）。
  * **底层通信**：管道（Pipe）与每台设备的 `adb shell` 高速异步通信；`exec-out` 二进制通道抓取原始帧/PNG。
  * **画面捕获**：`PrintWindow` 拦截 Scrcpy 渲染句柄实现 UI 内嵌镜像。
  * **多线程模型**：UI 渲染 / 脚本解释器 / HTTP 服务分离，互斥锁保护共享数据。
  * **模块布局**：`mockstick/*.h` header-only 模块（状态、ADB、宏引擎、HTTP、配置），单翻译单元编译。


## 📥 快速开始

### 环境依赖

  * **Windows 10/11** (x64)
  * **Android 设备**：需开启"开发者选项"中的"USB 调试"和"USB 调试（安全设置）"。

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

前往 [Releases](https://github.com/Vesper6/SkillGo-MockStick-RPG/releases) 或任意一次 [Actions](https://github.com/Vesper6/SkillGo-MockStick-RPG/actions) 构建下载 `MockStick-RPG.exe`。

### 运行说明

将 `MockStick-RPG.exe` 与以下文件放在同一目录：

  * `adb.exe`
  * `scrcpy.exe` 及其关联的 `.dll` 文件
  * `tools/ocr.ps1`（使用 OCR 功能时需要）


## 📖 使用指南

1.  **连接设备**：「设备」页点击"刷新设备列表"，勾选要控制的设备后点击"连接选中设备"。
2.  **摇杆设置**：「摇杆」页调整摇杆中心 X/Y 与滑动步长对准游戏摇杆，点击"保存参数配置"。
3.  **录制宏**：「宏与脚本」页开始录制 → 在镜像窗口操作 → 停止录制 → 可在"步骤编辑器"微调 → 保存。
4.  **高级脚本**：在"高级脚本"区编写条件 DSL（参考 `examples/advanced_demo.txt`），语法检查后运行。
5.  **AI 控制**：「自动化」页确认 HTTP API 运行中，按 [mcp/README.md](mcp/README.md) 接入 Claude，即可让 AI 看屏操作。
6.  **定时任务**：「自动化」页添加时间+脚本，软件运行期间到点自动执行。

### 高级脚本 DSL 速查

| 指令 | 说明 |
|---|---|
| `tap X Y` | 点击 |
| `swipe X1 Y1 X2 Y2 [MS]` | 滑动 |
| `key CODE` | 安卓按键 (4=返回 224=唤醒) |
| `text 内容` | 输入文本 (ASCII) |
| `wait 秒` / `random_wait 最小 最大` | 固定/随机等待 |
| `label 名` / `goto 名 [次数]` | 标签与跳转 (次数省略=无限) |
| `wait_pixel X Y RRGGBB [容差] [超时]` | 等待像素变色 |
| `if_pixel X Y RRGGBB 容差 标签` | 像素条件跳转 |
| `click_image 模板.mstr [相似度] [超时]` | 找图并点击 |
| `wait_text 文字 [超时]` | 等待 OCR 识别到文字 |
| `stop` | 结束脚本 |


## 📂 目录结构

```text
.
├── .github/workflows/  # CI 自动构建与发布
├── ci/                 # 交叉编译脚本 (本地与 CI 共用)
├── examples/           # 高级脚本 DSL 示例
├── externals/          # 第三方库 (Dear ImGui)
├── mcp/                # MCP Server (AI 接入) 与文档
├── mockstick/          # 核心模块 (状态/ADB/宏引擎/HTTP/配置)
├── tools/              # OCR 等辅助脚本
├── MockStick-RPG.cpp   # 主程序与 UI
├── resource.rc         # 资源定义 (图标/元数据)
├── build.bat           # Windows 本机构建脚本
└── README.md           # 项目文档
```


## 🛡️ 开源协议

本项目基于 [MIT License](LICENSE) 协议开源。


## 👨‍💻 作者

**Vesper** - [GitHub @Vesper6](https://github.com/Vesper6)
