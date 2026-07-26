// 全局状态与共享数据结构 (单翻译单元, 仅由 MockStick-RPG.cpp 包含一次)
#pragma once
#include <windows.h>
#include <cstdio>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include <map>
#include <deque>

// --- 设备 ---
struct Device {
    std::string serial;
    FILE* pipe = nullptr;   // adb -s <serial> shell 写管道
    bool selected = true;   // 是否接收广播指令
};
inline std::vector<Device> g_Devices;
inline std::mutex g_DevMutex;

// --- 摇杆与设备参数 ---
inline int g_PhoneW = 1080, g_PhoneH = 2340;
inline int g_JoyX = 540, g_JoyY = 1500, g_StepSize = 400;
inline int g_SwipeMs = 180;
inline int g_TapThreshold = 10;

// --- 宏 (简易录制序列) ---
struct AdbAction { int x1, y1, x2, y2, ms; float delay; };
inline std::vector<AdbAction> g_Script;
inline std::mutex g_ScriptMutex;
inline std::atomic<bool> g_IsRecording{ false }, g_IsPlaying{ false };
inline std::atomic<int> g_PlayedLoops{ 0 };
inline ULONGLONG g_LastActionTime = 0;
inline int g_LoopCount = 0;              // 0 = 无限
inline char g_ScriptFileName[128] = "rpg_macro_01.txt";

// --- 高级脚本 (DSL v2) ---
inline char g_AdvFileName[128] = "advanced_macro.txt";
inline char g_AdvScript[16384] = "#mockstick_v2\n# 示例: 每 5 秒点一次屏幕中心, 共 10 次\nlabel top\ntap 540 1170\nwait 5\ngoto top 9\n";
inline char g_AdvCheckMsg[256] = "";

// --- HTTP API ---
inline int g_HttpPort = 18300;
inline bool g_HttpEnabled = true;
inline std::atomic<bool> g_HttpRun{ false };

// --- 定时任务 ---
struct SchedTask {
    int hour = 8, minute = 0;
    bool enabled = true;
    char file[128] = "rpg_macro_01.txt";
    int lastRunDay = -1;    // 防止同一分钟内重复触发 (月*40+日)
};
inline std::vector<SchedTask> g_Sched;

// --- 界面状态 ---
inline char g_StatusMsg[256] = "";
inline char g_ProfileName[64] = "default";

inline const char* kConfigFile = "mockstick_config.ini";
