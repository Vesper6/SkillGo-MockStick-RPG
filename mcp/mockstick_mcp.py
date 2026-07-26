#!/usr/bin/env python3
"""MockStick-RPG MCP Server

将 MockStick-RPG 的 HTTP API 暴露为 MCP (Model Context Protocol) 工具,
让 Claude 等 AI 助手可以直接查看手机画面并执行点击/滑动/宏脚本。

仅依赖 Python 3.8+ 标准库, 通过 stdio (换行分隔 JSON-RPC) 与 MCP 客户端通信。
使用前请先启动 MockStick-RPG.exe 并确认 HTTP API 已开启 (默认端口 18300)。

环境变量:
    MOCKSTICK_URL  HTTP API 地址, 默认 http://127.0.0.1:18300
"""
import base64
import json
import os
import sys
import urllib.parse
import urllib.request

BASE_URL = os.environ.get("MOCKSTICK_URL", "http://127.0.0.1:18300")
SERVER_VERSION = "6.0.0"


def http_get(path, params=None, binary=False, timeout=60):
    url = BASE_URL + path
    if params:
        url += "?" + urllib.parse.urlencode(params)
    with urllib.request.urlopen(url, timeout=timeout) as resp:
        data = resp.read()
    return data if binary else data.decode("utf-8", "replace")


TOOLS = [
    {
        "name": "status",
        "description": "获取 MockStick 状态: 已连接的安卓设备列表、脚本运行状态、手机分辨率。开始操作前先调用此工具确认设备已连接。",
        "inputSchema": {"type": "object", "properties": {}, "required": []},
    },
    {
        "name": "screenshot",
        "description": "截取主设备当前屏幕画面 (PNG)。用于观察手机界面, 决定下一步操作。",
        "inputSchema": {"type": "object", "properties": {}, "required": []},
    },
    {
        "name": "tap",
        "description": "点击手机屏幕上的指定坐标 (以手机分辨率为准, 见 status 的 phone_w/phone_h)。",
        "inputSchema": {
            "type": "object",
            "properties": {
                "x": {"type": "integer", "description": "横坐标 (px)"},
                "y": {"type": "integer", "description": "纵坐标 (px)"},
            },
            "required": ["x", "y"],
        },
    },
    {
        "name": "swipe",
        "description": "在手机屏幕上滑动 (拖拽/翻页/摇杆移动)。",
        "inputSchema": {
            "type": "object",
            "properties": {
                "x1": {"type": "integer", "description": "起点 X"},
                "y1": {"type": "integer", "description": "起点 Y"},
                "x2": {"type": "integer", "description": "终点 X"},
                "y2": {"type": "integer", "description": "终点 Y"},
                "ms": {"type": "integer", "description": "滑动时长毫秒, 默认 300"},
            },
            "required": ["x1", "y1", "x2", "y2"],
        },
    },
    {
        "name": "key",
        "description": "发送安卓按键事件。常用: 3=Home, 4=返回, 24=音量加, 25=音量减, 26=电源, 224=唤醒屏幕。",
        "inputSchema": {
            "type": "object",
            "properties": {"code": {"type": "integer", "description": "Android keycode"}},
            "required": ["code"],
        },
    },
    {
        "name": "input_text",
        "description": "向当前聚焦的输入框输入文本 (仅支持 ASCII 字符)。",
        "inputSchema": {
            "type": "object",
            "properties": {"text": {"type": "string", "description": "要输入的文本"}},
            "required": ["text"],
        },
    },
    {
        "name": "ui_tree",
        "description": "获取当前界面的 UI 布局树 (uiautomator XML), 包含各控件的文字、类名与 bounds 坐标, 比截图更精确地定位可点击元素。",
        "inputSchema": {"type": "object", "properties": {}, "required": []},
    },
    {
        "name": "ocr",
        "description": "对当前屏幕执行 OCR 文字识别, 返回识别出的全部文字 (依赖 Windows 自带 OCR 引擎)。",
        "inputSchema": {"type": "object", "properties": {}, "required": []},
    },
    {
        "name": "get_pixel",
        "description": "读取屏幕指定坐标的像素颜色 (RGB), 用于判断状态 (如血条颜色、按钮是否亮起)。",
        "inputSchema": {
            "type": "object",
            "properties": {
                "x": {"type": "integer"},
                "y": {"type": "integer"},
            },
            "required": ["x", "y"],
        },
    },
    {
        "name": "find_image",
        "description": "在当前屏幕查找模板图片 (MSTR 格式, 由 MockStick 自动化页截取), 返回最佳匹配位置与相似度。",
        "inputSchema": {
            "type": "object",
            "properties": {
                "file": {"type": "string", "description": "模板文件路径 (相对 exe 目录)"},
                "thresh": {"type": "number", "description": "相似度阈值 0-1, 默认 0.9"},
            },
            "required": ["file"],
        },
    },
    {
        "name": "run_macro",
        "description": "执行已保存的宏脚本文件 (支持 v2 DSL 与旧格式)。",
        "inputSchema": {
            "type": "object",
            "properties": {
                "file": {"type": "string", "description": "脚本文件路径 (相对 exe 目录)"},
                "loops": {"type": "integer", "description": "循环次数, 0=无限, 默认 1"},
            },
            "required": ["file"],
        },
    },
    {
        "name": "stop_macro",
        "description": "停止当前正在运行的宏脚本。",
        "inputSchema": {"type": "object", "properties": {}, "required": []},
    },
]


def text_content(text):
    return [{"type": "text", "text": text}]


def call_tool(name, args):
    if name == "status":
        return text_content(http_get("/status"))
    if name == "screenshot":
        png = http_get("/screenshot", binary=True)
        if png[:4] == b"\x89PNG":
            return [{"type": "image", "data": base64.b64encode(png).decode(), "mimeType": "image/png"}]
        return text_content("截图失败: " + png.decode("utf-8", "replace"))
    if name == "tap":
        return text_content(http_get("/tap", {"x": args["x"], "y": args["y"]}))
    if name == "swipe":
        return text_content(http_get("/swipe", {
            "x1": args["x1"], "y1": args["y1"], "x2": args["x2"], "y2": args["y2"],
            "ms": args.get("ms", 300),
        }))
    if name == "key":
        return text_content(http_get("/key", {"code": args["code"]}))
    if name == "input_text":
        return text_content(http_get("/text", {"s": args["text"]}))
    if name == "ui_tree":
        return text_content(http_get("/uidump", timeout=120))
    if name == "ocr":
        return text_content(http_get("/ocr", timeout=120))
    if name == "get_pixel":
        return text_content(http_get("/pixel", {"x": args["x"], "y": args["y"]}))
    if name == "find_image":
        return text_content(http_get("/find_image", {"file": args["file"], "thresh": args.get("thresh", 0.9)}, timeout=120))
    if name == "run_macro":
        return text_content(http_get("/macro/run", {"file": args["file"], "loops": args.get("loops", 1)}))
    if name == "stop_macro":
        return text_content(http_get("/macro/stop"))
    raise ValueError("未知工具: " + name)


def send(msg):
    sys.stdout.write(json.dumps(msg, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def main():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except json.JSONDecodeError:
            continue
        mid = msg.get("id")
        method = msg.get("method", "")

        if method.startswith("notifications/"):
            continue

        if method == "initialize":
            proto = (msg.get("params") or {}).get("protocolVersion", "2024-11-05")
            result = {
                "protocolVersion": proto,
                "capabilities": {"tools": {}},
                "serverInfo": {"name": "mockstick-rpg", "version": SERVER_VERSION},
            }
        elif method == "ping":
            result = {}
        elif method == "tools/list":
            result = {"tools": TOOLS}
        elif method == "tools/call":
            params = msg.get("params") or {}
            try:
                content = call_tool(params.get("name", ""), params.get("arguments") or {})
                result = {"content": content, "isError": False}
            except Exception as e:  # noqa: BLE001 - 所有异常都转为工具错误返回
                result = {
                    "content": text_content(
                        f"调用失败: {e}\n请确认 MockStick-RPG.exe 已启动且 HTTP API 运行中 ({BASE_URL})。"
                    ),
                    "isError": True,
                }
        else:
            if mid is not None:
                send({"jsonrpc": "2.0", "id": mid, "error": {"code": -32601, "message": f"未实现的方法: {method}"}})
            continue

        if mid is not None:
            send({"jsonrpc": "2.0", "id": mid, "result": result})


if __name__ == "__main__":
    main()
