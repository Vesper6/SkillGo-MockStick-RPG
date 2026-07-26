// 日志与通用工具
#pragma once
#include "state.h"
#include <cstdarg>
#include <cstring>
#include <cctype>

inline std::deque<std::string> g_LogLines;
inline std::mutex g_LogMutex;

// 写入 logs/ 当日文件并保留在内存环形缓冲供 UI 展示
inline void LogMsg(const char* fmt, ...) {
    char buf[1024];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    SYSTEMTIME st; GetLocalTime(&st);
    char line[1200];
    snprintf(line, sizeof(line), "[%02d:%02d:%02d] %s", st.wHour, st.wMinute, st.wSecond, buf);
    {
        std::lock_guard<std::mutex> lk(g_LogMutex);
        g_LogLines.push_back(line);
        while (g_LogLines.size() > 300) g_LogLines.pop_front();
    }
    CreateDirectoryA("logs", NULL);
    char fn[64];
    snprintf(fn, sizeof(fn), "logs/mockstick_%04d%02d%02d.log", st.wYear, st.wMonth, st.wDay);
    FILE* f = fopen(fn, "ab");
    if (f) { fprintf(f, "%s\r\n", line); fclose(f); }
}

inline std::string UrlDecode(const std::string& s) {
    std::string r; r.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '+') r += ' ';
        else if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
            char hex[3] = { s[i + 1], s[i + 2], 0 };
            r += (char)strtol(hex, nullptr, 16);
            i += 2;
        }
        else r += s[i];
    }
    return r;
}

// 解析 "a=1&b=xx" 为键值表 (值已 URL 解码)
inline std::map<std::string, std::string> ParseQuery(const std::string& q) {
    std::map<std::string, std::string> m;
    size_t pos = 0;
    while (pos < q.size()) {
        size_t amp = q.find('&', pos);
        if (amp == std::string::npos) amp = q.size();
        std::string kv = q.substr(pos, amp - pos);
        size_t eq = kv.find('=');
        if (eq != std::string::npos) m[kv.substr(0, eq)] = UrlDecode(kv.substr(eq + 1));
        else if (!kv.empty()) m[kv] = "";
        pos = amp + 1;
    }
    return m;
}

inline int QInt(const std::map<std::string, std::string>& q, const char* k, int def) {
    auto it = q.find(k);
    return it == q.end() ? def : atoi(it->second.c_str());
}
inline float QFloat(const std::map<std::string, std::string>& q, const char* k, float def) {
    auto it = q.find(k);
    return it == q.end() ? def : (float)atof(it->second.c_str());
}
inline std::string QStr(const std::map<std::string, std::string>& q, const char* k, const char* def) {
    auto it = q.find(k);
    return it == q.end() ? std::string(def) : it->second;
}

// 简易 JSON 字符串转义 (足够本项目使用)
inline std::string JsonEscape(const std::string& s) {
    std::string r; r.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '"': r += "\\\""; break;
        case '\\': r += "\\\\"; break;
        case '\n': r += "\\n"; break;
        case '\r': r += "\\r"; break;
        case '\t': r += "\\t"; break;
        default:
            if ((unsigned char)c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); r += b; }
            else r += c;
        }
    }
    return r;
}
