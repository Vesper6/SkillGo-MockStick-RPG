// ADB 多设备管理、指令广播、屏幕捕获、UI 树与 OCR
#pragma once
#include "state.h"
#include "util.h"
#include <fstream>
#include <cstdint>

// 运行命令并收集全部输出 (binary=true 用于截图等二进制流)
inline bool ExecCapture(const char* cmd, std::string& out, bool binary = false) {
    FILE* p = _popen(cmd, binary ? "rb" : "r");
    if (!p) return false;
    char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
    _pclose(p);
    return true;
}

// 扫描 adb devices, 增量更新设备列表 (保留已有管道与勾选状态)
inline void ScanDevices() {
    std::string out;
    ExecCapture("adb devices", out);
    std::vector<std::string> found;
    size_t pos = 0; int line = 0;
    while (pos < out.size()) {
        size_t nl = out.find('\n', pos);
        if (nl == std::string::npos) nl = out.size();
        std::string s = out.substr(pos, nl - pos);
        size_t p = s.find("\tdevice");
        if (line > 0 && p != std::string::npos) found.push_back(s.substr(0, p));
        pos = nl + 1; line++;
    }
    std::lock_guard<std::mutex> lk(g_DevMutex);
    // 移除消失的设备并关闭其管道
    for (auto it = g_Devices.begin(); it != g_Devices.end();) {
        bool alive = false;
        for (auto& f : found) if (f == it->serial) { alive = true; break; }
        if (!alive) {
            if (it->pipe) _pclose(it->pipe);
            LogMsg("设备离线: %s", it->serial.c_str());
            it = g_Devices.erase(it);
        }
        else ++it;
    }
    // 添加新设备
    for (auto& f : found) {
        bool known = false;
        for (auto& d : g_Devices) if (d.serial == f) { known = true; break; }
        if (!known) { g_Devices.push_back({ f, nullptr, true }); LogMsg("发现设备: %s", f.c_str()); }
    }
}

// 主设备 = 第一个勾选的设备 (镜像/截图/OCR 均针对主设备)
inline std::string PrimarySerial() {
    std::lock_guard<std::mutex> lk(g_DevMutex);
    for (auto& d : g_Devices) if (d.selected) return d.serial;
    return "";
}

inline void ConnectDevices() {
    std::lock_guard<std::mutex> lk(g_DevMutex);
    for (auto& d : g_Devices) {
        if (d.selected && !d.pipe) {
            char cmd[160]; snprintf(cmd, sizeof(cmd), "adb -s %s shell", d.serial.c_str());
            d.pipe = _popen(cmd, "w");
            if (d.pipe) LogMsg("已打开 shell 管道: %s", d.serial.c_str());
            else LogMsg("打开管道失败: %s", d.serial.c_str());
        }
    }
}

inline void DisconnectDevices() {
    std::lock_guard<std::mutex> lk(g_DevMutex);
    for (auto& d : g_Devices) {
        if (d.pipe) { _pclose(d.pipe); d.pipe = nullptr; }
    }
    LogMsg("已断开所有设备管道");
}

// 广播 shell 指令到所有勾选设备 (线程安全: UI/回放/HTTP 线程共用)
inline void RunAdb(const char* cmd) {
    std::lock_guard<std::mutex> lk(g_DevMutex);
    for (auto& d : g_Devices) {
        if (d.selected && d.pipe) { fprintf(d.pipe, "%s\n", cmd); fflush(d.pipe); }
    }
}

inline void Swipe(int x1, int y1, int x2, int y2, int ms) {
    char c[160];
    // 位移极小时发送 tap, 避免原地 swipe 被游戏识别为长按
    if (abs(x2 - x1) < g_TapThreshold && abs(y2 - y1) < g_TapThreshold)
        snprintf(c, sizeof(c), "input tap %d %d", x1, y1);
    else
        snprintf(c, sizeof(c), "input swipe %d %d %d %d %d", x1, y1, x2, y2, ms);
    RunAdb(c);
    if (g_IsRecording) {
        std::lock_guard<std::mutex> lk(g_ScriptMutex);
        float d = g_Script.empty() ? 0 : (float)(GetTickCount64() - g_LastActionTime) / 1000.0f;
        g_Script.push_back({ x1, y1, x2, y2, ms, d });
        g_LastActionTime = GetTickCount64();
    }
}

// 抓取主设备原始帧 (RGBA)。screencap 头部为 12 或 16 字节 (Android 版本差异), 按数据量自适应
inline bool CaptureRaw(std::vector<unsigned char>& rgba, int& w, int& h) {
    std::string serial = PrimarySerial();
    if (serial.empty()) return false;
    char cmd[256]; snprintf(cmd, sizeof(cmd), "adb -s %s exec-out screencap", serial.c_str());
    std::string data;
    if (!ExecCapture(cmd, data, true) || data.size() < 16) return false;
    auto le32 = [&](size_t o) -> int {
        return (int)((unsigned char)data[o] | ((unsigned char)data[o + 1] << 8) | ((unsigned char)data[o + 2] << 16) | ((unsigned char)data[o + 3] << 24));
        };
    w = le32(0); h = le32(4);
    if (w <= 0 || h <= 0 || w > 10000 || h > 10000) return false;
    size_t need = (size_t)w * h * 4;
    size_t off = (data.size() >= 16 + need) ? 16 : 12;
    if (data.size() < off + need) return false;
    rgba.assign(data.begin() + off, data.begin() + off + need);
    return true;
}

// 主设备 PNG 截图 (直接转发 screencap -p 的字节流)
inline bool CapturePng(std::string& png) {
    std::string serial = PrimarySerial();
    if (serial.empty()) return false;
    char cmd[256]; snprintf(cmd, sizeof(cmd), "adb -s %s exec-out screencap -p", serial.c_str());
    return ExecCapture(cmd, png, true) && png.size() > 8;
}

inline bool CapturePngToFile(const char* path) {
    std::string png;
    if (!CapturePng(png)) return false;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(png.data(), (std::streamsize)png.size());
    return true;
}

inline bool PixelMatch(int x, int y, int rgb, int tol) {
    std::vector<unsigned char> px; int w, h;
    if (!CaptureRaw(px, w, h)) return false;
    if (x < 0 || y < 0 || x >= w || y >= h) return false;
    const unsigned char* p = &px[((size_t)y * w + x) * 4];
    int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    return abs(p[0] - r) <= tol && abs(p[1] - g) <= tol && abs(p[2] - b) <= tol;
}

// UI 布局树 (uiautomator dump), 供 AI 分析界面元素
inline std::string UiDump() {
    std::string serial = PrimarySerial();
    if (serial.empty()) return "";
    char cmd[300]; std::string tmp;
    snprintf(cmd, sizeof(cmd), "adb -s %s shell uiautomator dump /sdcard/mockstick_ui.xml", serial.c_str());
    ExecCapture(cmd, tmp);
    std::string xml;
    snprintf(cmd, sizeof(cmd), "adb -s %s exec-out cat /sdcard/mockstick_ui.xml", serial.c_str());
    ExecCapture(cmd, xml, true);
    return xml;
}

// OCR: 截图到临时文件后调用 tools/ocr.ps1 (Windows.Media.Ocr)
inline std::string RunOcr() {
    char tmpPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpPath);
    std::string png = std::string(tmpPath) + "mockstick_ocr.png";
    if (!CapturePngToFile(png.c_str())) { LogMsg("OCR: 截图失败"); return ""; }
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "powershell -NoProfile -ExecutionPolicy Bypass -File tools\\ocr.ps1 \"%s\"", png.c_str());
    std::string out;
    ExecCapture(cmd, out);
    // 去除结尾换行
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}
