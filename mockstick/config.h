// 配置持久化 (v2 键值格式, 兼容旧版空格分隔格式)、配置档案、定时任务调度
#pragma once
#include "state.h"
#include "util.h"
#include "macro.h"
#include <fstream>
#include <sstream>

inline void SaveConfigTo(const char* path) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) return;
    ofs << "#mockstick_cfg_v2\n";
    ofs << "phone_w=" << g_PhoneW << "\nphone_h=" << g_PhoneH << "\n";
    ofs << "joy_x=" << g_JoyX << "\njoy_y=" << g_JoyY << "\n";
    ofs << "step=" << g_StepSize << "\nswipe_ms=" << g_SwipeMs << "\n";
    ofs << "loop=" << g_LoopCount << "\n";
    ofs << "script=" << g_ScriptFileName << "\n";
    ofs << "adv_script=" << g_AdvFileName << "\n";
    ofs << "http_port=" << g_HttpPort << "\nhttp_enabled=" << (g_HttpEnabled ? 1 : 0) << "\n";
    for (auto& s : g_Sched)
        ofs << "sched=" << s.hour << " " << s.minute << " " << (s.enabled ? 1 : 0) << " " << s.file << "\n";
}

inline void LoadConfigFrom(const char* path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) return;
    std::string first;
    std::getline(ifs, first);
    if (first.rfind("#mockstick_cfg_v2", 0) != 0) {
        // 旧格式: 一行数字 + 一行脚本名
        std::istringstream l1(first);
        int pw, ph, jx, jy, step, ms, loop;
        if (l1 >> pw >> ph >> jx >> jy >> step >> ms >> loop) {
            if (pw > 0 && ph > 0) { g_PhoneW = pw; g_PhoneH = ph; }
            g_JoyX = jx; g_JoyY = jy;
            if (step > 0) g_StepSize = step;
            if (ms > 0) g_SwipeMs = ms;
            if (loop >= 0) g_LoopCount = loop;
            std::string name;
            if (std::getline(ifs, name) && !name.empty()) snprintf(g_ScriptFileName, sizeof(g_ScriptFileName), "%s", name.c_str());
        }
        return;
    }
    g_Sched.clear();
    std::string line;
    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "phone_w") { int x = atoi(v.c_str()); if (x > 0) g_PhoneW = x; }
        else if (k == "phone_h") { int x = atoi(v.c_str()); if (x > 0) g_PhoneH = x; }
        else if (k == "joy_x") g_JoyX = atoi(v.c_str());
        else if (k == "joy_y") g_JoyY = atoi(v.c_str());
        else if (k == "step") { int x = atoi(v.c_str()); if (x > 0) g_StepSize = x; }
        else if (k == "swipe_ms") { int x = atoi(v.c_str()); if (x > 0) g_SwipeMs = x; }
        else if (k == "loop") { int x = atoi(v.c_str()); if (x >= 0) g_LoopCount = x; }
        else if (k == "script" && !v.empty()) snprintf(g_ScriptFileName, sizeof(g_ScriptFileName), "%s", v.c_str());
        else if (k == "adv_script" && !v.empty()) snprintf(g_AdvFileName, sizeof(g_AdvFileName), "%s", v.c_str());
        else if (k == "http_port") { int x = atoi(v.c_str()); if (x > 0 && x < 65536) g_HttpPort = x; }
        else if (k == "http_enabled") g_HttpEnabled = atoi(v.c_str()) != 0;
        else if (k == "sched") {
            SchedTask t;
            std::istringstream ss(v);
            int en = 1;
            std::string file;
            if (ss >> t.hour >> t.minute >> en) {
                std::getline(ss, file);
                while (!file.empty() && file.front() == ' ') file.erase(file.begin());
                t.enabled = en != 0;
                if (!file.empty()) snprintf(t.file, sizeof(t.file), "%s", file.c_str());
                if (t.hour >= 0 && t.hour < 24 && t.minute >= 0 && t.minute < 60) g_Sched.push_back(t);
            }
        }
    }
}

inline void SaveConfig() { SaveConfigTo(kConfigFile); }
inline void LoadConfig() { LoadConfigFrom(kConfigFile); }

// ---------- 配置档案 ----------
inline std::vector<std::string> ListProfiles() {
    std::vector<std::string> out;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA("profiles\\*.ini", &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        std::string name = fd.cFileName;
        if (name.size() > 4) out.push_back(name.substr(0, name.size() - 4));
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return out;
}

inline bool SaveProfile(const char* name) {
    if (!name[0]) return false;
    CreateDirectoryA("profiles", NULL);
    char path[192]; snprintf(path, sizeof(path), "profiles\\%s.ini", name);
    SaveConfigTo(path);
    LogMsg("档案已保存: %s", name);
    return true;
}

inline bool LoadProfile(const char* name) {
    char path[192]; snprintf(path, sizeof(path), "profiles\\%s.ini", name);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) return false;
    LoadConfigFrom(path);
    LogMsg("档案已载入: %s", name);
    return true;
}

inline bool DeleteProfile(const char* name) {
    char path[192]; snprintf(path, sizeof(path), "profiles\\%s.ini", name);
    return DeleteFileA(path) != 0;
}

// ---------- 定时任务调度 (主循环每秒检查一次) ----------
inline void SchedulerTick() {
    static ULONGLONG lastCheck = 0;
    if (GetTickCount64() - lastCheck < 1000) return;
    lastCheck = GetTickCount64();
    SYSTEMTIME st; GetLocalTime(&st);
    int today = st.wMonth * 40 + st.wDay;
    for (auto& t : g_Sched) {
        if (!t.enabled || t.hour != st.wHour || t.minute != st.wMinute || t.lastRunDay == today) continue;
        t.lastRunDay = today;
        if (g_IsPlaying) { LogMsg("定时任务 %02d:%02d 跳过: 已有脚本在运行", t.hour, t.minute); continue; }
        RunAdb("input keyevent 224");   // 唤醒屏幕
        LogMsg("定时任务触发 %02d:%02d -> %s", t.hour, t.minute, t.file);
        StartMacroFile(t.file, 1);
    }
}
