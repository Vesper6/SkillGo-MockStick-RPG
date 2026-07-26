# MockStick-RPG MCP Server — 让 AI 控制你的手机

`mockstick_mcp.py` 把 MockStick-RPG 的本地 HTTP API 封装为 [MCP (Model Context Protocol)](https://modelcontextprotocol.io) 工具集。接入后，Claude 等 AI 助手可以：

- 📷 **看**：`screenshot` 截屏、`ui_tree` 读取界面控件树、`ocr` 识别屏幕文字、`get_pixel` 读像素颜色
- 👆 **操作**：`tap` 点击、`swipe` 滑动、`key` 按键、`input_text` 输入文本
- 🤖 **编排**：`run_macro` / `stop_macro` 执行宏脚本、`find_image` 找图定位

也就是说，你可以直接对 AI 说"帮我把每日任务做完"，它会自己截图 → 分析界面 → 点击操作。

## 前置条件

1. Windows 10/11，已安装 Python 3.8+（`python --version` 验证）
2. 启动 `MockStick-RPG.exe`，在"自动化"页确认 HTTP API 运行中（默认 `http://127.0.0.1:18300`）
3. 在"设备"页连接好安卓设备

无需安装任何第三方 Python 包（纯标准库实现）。

## 接入 Claude Code

```bash
claude mcp add mockstick -- python C:\path\to\SkillGo-MockStick-RPG\mcp\mockstick_mcp.py
```

## 接入 Claude Desktop

编辑 `%APPDATA%\Claude\claude_desktop_config.json`：

```json
{
  "mcpServers": {
    "mockstick": {
      "command": "python",
      "args": ["C:\\path\\to\\SkillGo-MockStick-RPG\\mcp\\mockstick_mcp.py"]
    }
  }
}
```

重启 Claude Desktop 后，对话中即可看到 mockstick 工具组。

## 自定义端口

若在 MockStick 中修改了 HTTP 端口，为 MCP 进程设置环境变量：

```json
{
  "mcpServers": {
    "mockstick": {
      "command": "python",
      "args": ["C:\\path\\to\\mcp\\mockstick_mcp.py"],
      "env": { "MOCKSTICK_URL": "http://127.0.0.1:28300" }
    }
  }
}
```

## 使用示例

接入后可以这样对 AI 说：

> "截个图看看现在游戏什么界面，然后帮我点掉所有弹窗"

> "读一下 UI 树，找到『每日签到』按钮并点击"

> "运行 rpg_macro_01.txt 宏 5 次，结束后截图确认"

## 安全说明

- HTTP API 仅监听 `127.0.0.1`，外部网络无法访问
- MCP 工具能做的操作与你手动在 MockStick 界面上能做的完全一致
- AI 的每一次点击都会经过 MockStick 的日志系统（"日志"页可回溯）
