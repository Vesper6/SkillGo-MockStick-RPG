// 宏引擎: DSL v2 解析器/解释器、模板匹配 (找图)、兼容旧 6 元组格式
#pragma once
#include "state.h"
#include "util.h"
#include "adb.h"
#include <sstream>
#include <algorithm>

// ---------- 模板图像 (自有 MSTR 格式: "MSTR" + int32 w + int32 h + RGBA) ----------
struct Img { int w = 0, h = 0; std::vector<unsigned char> rgba; };

inline bool LoadTemplateFile(const char* path, Img& t) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char magic[4] = { 0 };
    f.read(magic, 4);
    if (memcmp(magic, "MSTR", 4) != 0) return false;
    int32_t w = 0, h = 0;
    f.read((char*)&w, 4); f.read((char*)&h, 4);
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return false;
    t.w = w; t.h = h;
    t.rgba.resize((size_t)w * h * 4);
    f.read((char*)t.rgba.data(), (std::streamsize)t.rgba.size());
    return (bool)f;
}

// 从当前主设备屏幕截取区域保存为模板文件
inline bool SaveRegionFile(int x, int y, int w, int h, const char* path) {
    std::vector<unsigned char> px; int sw, sh;
    if (!CaptureRaw(px, sw, sh)) return false;
    if (x < 0) x = 0; if (y < 0) y = 0;
    if (x + w > sw) w = sw - x;
    if (y + h > sh) h = sh - y;
    if (w <= 0 || h <= 0) return false;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    int32_t w32 = w, h32 = h;
    f.write("MSTR", 4);
    f.write((char*)&w32, 4); f.write((char*)&h32, 4);
    for (int row = 0; row < h; row++)
        f.write((char*)&px[(((size_t)y + row) * sw + x) * 4], (std::streamsize)w * 4);
    return (bool)f;
}

inline int GrayAt(const unsigned char* p, int stride, int x, int y) {
    const unsigned char* q = p + ((size_t)y * stride + x) * 4;
    return q[0] + q[1] + q[2];
}

// 归一化相似度 (1 = 完全一致), 采样步长 step
inline double MatchAt(const unsigned char* scr, int sw, const Img& t, int px, int py, int step) {
    long long sum = 0; int cnt = 0;
    for (int ty = 0; ty < t.h; ty += step)
        for (int tx = 0; tx < t.w; tx += step) {
            sum += abs(GrayAt(scr, sw, px + tx, py + ty) - GrayAt(t.rgba.data(), t.w, tx, ty));
            cnt++;
        }
    if (!cnt) return 0;
    return 1.0 - (double)sum / cnt / 765.0;
}

// 粗到细搜索模板, 返回最佳匹配中心坐标与相似度
inline bool FindImageOnScreen(const Img& t, int& outX, int& outY, double& outSim) {
    std::vector<unsigned char> px; int sw, sh;
    if (!CaptureRaw(px, sw, sh)) return false;
    if (t.w > sw || t.h > sh) return false;
    int bestX = 0, bestY = 0; double best = -1;
    for (int y = 0; y <= sh - t.h; y += 4)
        for (int x = 0; x <= sw - t.w; x += 4) {
            double s = MatchAt(px.data(), sw, t, x, y, 4);
            if (s > best) { best = s; bestX = x; bestY = y; }
        }
    // 在粗匹配附近细化
    int x0 = bestX > 6 ? bestX - 6 : 0, y0 = bestY > 6 ? bestY - 6 : 0;
    int x1 = (bestX + 6 <= sw - t.w) ? bestX + 6 : sw - t.w;
    int y1 = (bestY + 6 <= sh - t.h) ? bestY + 6 : sh - t.h;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            double s = MatchAt(px.data(), sw, t, x, y, 2);
            if (s > best) { best = s; bestX = x; bestY = y; }
        }
    outX = bestX + t.w / 2; outY = bestY + t.h / 2; outSim = best;
    return true;
}

// ---------- DSL v2 ----------
struct MacroOp {
    enum Type { TAP, SWIPE, KEY, TEXT, WAIT, RAND_WAIT, LABEL, GOTO, WAIT_PIXEL, IF_PIXEL, CLICK_IMAGE, WAIT_TEXT, STOP } t;
    int a = 0, b = 0, c = 0, d = 0, e = 0;
    float f1 = 0, f2 = 0;
    std::string s1;
    int jump = -1;          // GOTO/IF_PIXEL 解析后的目标指令下标
    int line = 0;           // 源文件行号 (报错用)
};

struct MacroProgram {
    std::vector<MacroOp> ops;
    std::string error;      // 非空 = 解析失败
};

inline bool ParseHexColor(const std::string& s, int& rgb) {
    if (s.empty()) return false;
    const char* p = s.c_str();
    if (*p == '#') p++;
    char* endp = nullptr;
    long v = strtol(p, &endp, 16);
    if (endp == p || *endp) return false;
    rgb = (int)v;
    return true;
}

inline bool ParseMacro(const std::string& text, MacroProgram& prog) {
    prog.ops.clear(); prog.error.clear();
    std::map<std::string, int> labels;
    std::istringstream iss(text);
    std::string lineStr;
    int lineNo = 0;
    auto fail = [&](const char* msg) {
        char b[256]; snprintf(b, sizeof(b), "第 %d 行: %s", lineNo, msg);
        prog.error = b;
        return false;
        };
    while (std::getline(iss, lineStr)) {
        lineNo++;
        if (!lineStr.empty() && lineStr.back() == '\r') lineStr.pop_back();
        std::istringstream ls(lineStr);
        std::string cmd;
        if (!(ls >> cmd)) continue;
        if (cmd[0] == '#') continue;
        std::transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char ch) { return (char)tolower(ch); });
        MacroOp op; op.line = lineNo;
        if (cmd == "tap") {
            op.t = MacroOp::TAP;
            if (!(ls >> op.a >> op.b)) return fail("tap 需要参数: X Y");
        }
        else if (cmd == "swipe") {
            op.t = MacroOp::SWIPE;
            if (!(ls >> op.a >> op.b >> op.c >> op.d)) return fail("swipe 需要参数: X1 Y1 X2 Y2 [MS]");
            if (!(ls >> op.e)) op.e = 300;
        }
        else if (cmd == "key") {
            op.t = MacroOp::KEY;
            if (!(ls >> op.a)) return fail("key 需要参数: KEYCODE");
        }
        else if (cmd == "text") {
            op.t = MacroOp::TEXT;
            std::getline(ls, op.s1);
            while (!op.s1.empty() && op.s1.front() == ' ') op.s1.erase(op.s1.begin());
            if (op.s1.empty()) return fail("text 需要参数: 文本内容 (仅支持 ASCII)");
        }
        else if (cmd == "wait") {
            op.t = MacroOp::WAIT;
            if (!(ls >> op.f1) || op.f1 < 0) return fail("wait 需要参数: 秒数");
        }
        else if (cmd == "random_wait") {
            op.t = MacroOp::RAND_WAIT;
            if (!(ls >> op.f1 >> op.f2) || op.f1 < 0 || op.f2 < op.f1) return fail("random_wait 需要参数: 最小秒 最大秒");
        }
        else if (cmd == "label") {
            op.t = MacroOp::LABEL;
            if (!(ls >> op.s1)) return fail("label 需要参数: 名称");
            if (labels.count(op.s1)) return fail("label 名称重复");
            labels[op.s1] = (int)prog.ops.size();
        }
        else if (cmd == "goto") {
            op.t = MacroOp::GOTO;
            if (!(ls >> op.s1)) return fail("goto 需要参数: 标签名 [次数]");
            if (!(ls >> op.a)) op.a = 0;   // 0 = 无限跳转
            if (op.a < 0) op.a = 0;
        }
        else if (cmd == "wait_pixel") {
            op.t = MacroOp::WAIT_PIXEL;
            std::string col;
            if (!(ls >> op.a >> op.b >> col)) return fail("wait_pixel 需要参数: X Y RRGGBB [容差] [超时秒]");
            if (!ParseHexColor(col, op.c)) return fail("颜色格式应为 RRGGBB 十六进制");
            if (!(ls >> op.d)) op.d = 25;
            if (!(ls >> op.f1)) op.f1 = 30;
        }
        else if (cmd == "if_pixel") {
            op.t = MacroOp::IF_PIXEL;
            std::string col;
            if (!(ls >> op.a >> op.b >> col)) return fail("if_pixel 需要参数: X Y RRGGBB 容差 标签名");
            if (!ParseHexColor(col, op.c)) return fail("颜色格式应为 RRGGBB 十六进制");
            if (!(ls >> op.d >> op.s1)) return fail("if_pixel 需要参数: X Y RRGGBB 容差 标签名");
        }
        else if (cmd == "click_image") {
            op.t = MacroOp::CLICK_IMAGE;
            if (!(ls >> op.s1)) return fail("click_image 需要参数: 模板文件 [相似度0-1] [超时秒]");
            if (!(ls >> op.f1)) op.f1 = 0.90f;
            if (!(ls >> op.f2)) op.f2 = 15;
        }
        else if (cmd == "wait_text") {
            op.t = MacroOp::WAIT_TEXT;
            if (!(ls >> op.s1)) return fail("wait_text 需要参数: 文字 [超时秒]");
            if (!(ls >> op.f1)) op.f1 = 30;
        }
        else if (cmd == "stop") {
            op.t = MacroOp::STOP;
        }
        else return fail("未知指令");
        prog.ops.push_back(op);
    }
    // 解析跳转目标
    for (auto& op : prog.ops) {
        if (op.t == MacroOp::GOTO || op.t == MacroOp::IF_PIXEL) {
            auto it = labels.find(op.s1);
            if (it == labels.end()) {
                lineNo = op.line;
                return fail("跳转目标标签不存在");
            }
            op.jump = it->second;
        }
    }
    return true;
}

// 旧 6 元组文件 → 程序; 或简易录制序列 → 程序
inline void VectorToProgram(const std::vector<AdbAction>& v, MacroProgram& prog) {
    prog.ops.clear(); prog.error.clear();
    for (auto& a : v) {
        if (a.delay > 0) { MacroOp w; w.t = MacroOp::WAIT; w.f1 = a.delay; prog.ops.push_back(w); }
        MacroOp op;
        if (abs(a.x2 - a.x1) < g_TapThreshold && abs(a.y2 - a.y1) < g_TapThreshold) { op.t = MacroOp::TAP; op.a = a.x1; op.b = a.y1; }
        else { op.t = MacroOp::SWIPE; op.a = a.x1; op.b = a.y1; op.c = a.x2; op.d = a.y2; op.e = a.ms; }
        prog.ops.push_back(op);
    }
}

// 程序 → 简易序列 (仅当只含 WAIT/TAP/SWIPE 时可转换, 供表格编辑器使用)
inline bool ProgramToVector(const MacroProgram& prog, std::vector<AdbAction>& v) {
    v.clear();
    float pendingWait = 0;
    for (auto& op : prog.ops) {
        if (op.t == MacroOp::WAIT) { pendingWait += op.f1; continue; }
        if (op.t == MacroOp::TAP) { v.push_back({ op.a, op.b, op.a, op.b, 100, pendingWait }); pendingWait = 0; continue; }
        if (op.t == MacroOp::SWIPE) { v.push_back({ op.a, op.b, op.c, op.d, op.e, pendingWait }); pendingWait = 0; continue; }
        if (op.t == MacroOp::LABEL) continue;  // 无副作用, 允许忽略
        return false;
    }
    return true;
}

// 从文件加载: 自动识别 v2 DSL 与旧数字格式
inline bool ProgramFromFile(const char* path, MacroProgram& prog) {
    std::ifstream f(path);
    if (!f.is_open()) { prog.error = "无法打开文件"; return false; }
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (text.rfind("#mockstick_v2", 0) == 0) return ParseMacro(text, prog);
    // 旧格式: 每行 6 个数字
    std::vector<AdbAction> v;
    std::istringstream iss(text);
    AdbAction a;
    while (iss >> a.x1 >> a.y1 >> a.x2 >> a.y2 >> a.ms >> a.delay) v.push_back(a);
    if (v.empty()) { prog.error = "文件为空或格式无法识别"; return false; }
    VectorToProgram(v, prog);
    return true;
}

// 保存简易序列为 v2 格式
inline void SaveVectorAsV2(const char* path, const std::vector<AdbAction>& v) {
    std::ofstream ofs(path);
    ofs << "#mockstick_v2\n";
    for (auto& a : v) {
        if (a.delay > 0) ofs << "wait " << a.delay << "\n";
        if (abs(a.x2 - a.x1) < g_TapThreshold && abs(a.y2 - a.y1) < g_TapThreshold)
            ofs << "tap " << a.x1 << " " << a.y1 << "\n";
        else
            ofs << "swipe " << a.x1 << " " << a.y1 << " " << a.x2 << " " << a.y2 << " " << a.ms << "\n";
    }
}

// ---------- 解释器 ----------
inline void InterruptibleWait(float sec) {
    DWORD ms = (DWORD)(sec * 1000);
    while (ms > 0 && g_IsPlaying) { DWORD s = ms > 50 ? 50 : ms; Sleep(s); ms -= s; }
}

inline void RunProgramBody(const MacroProgram& prog, int loops) {
    LogMsg("脚本开始: %d 条指令, 循环 %s", (int)prog.ops.size(), loops == 0 ? "无限" : std::to_string(loops).c_str());
    int done = 0;
    std::vector<int> gotoLeft(prog.ops.size(), 0);
    while (g_IsPlaying && (loops == 0 || done < loops)) {
        for (size_t i = 0; i < prog.ops.size(); i++)
            gotoLeft[i] = (prog.ops[i].t == MacroOp::GOTO) ? prog.ops[i].a : 0;
        size_t pc = 0;
        while (pc < prog.ops.size() && g_IsPlaying) {
            const MacroOp& op = prog.ops[pc];
            switch (op.t) {
            case MacroOp::TAP: Swipe(op.a, op.b, op.a, op.b, 100); break;
            case MacroOp::SWIPE: Swipe(op.a, op.b, op.c, op.d, op.e); break;
            case MacroOp::KEY: { char c[64]; snprintf(c, sizeof(c), "input keyevent %d", op.a); RunAdb(c); } break;
            case MacroOp::TEXT: {
                std::string cmd = "input text ";
                for (char ch : op.s1) { if (ch == ' ') cmd += "%s"; else cmd += ch; }  // adb 空格转义
                RunAdb(cmd.c_str());
            } break;
            case MacroOp::WAIT: InterruptibleWait(op.f1); break;
            case MacroOp::RAND_WAIT: {
                float span = op.f2 - op.f1;
                float sec = op.f1 + span * (float)(rand() % 1000) / 1000.0f;
                InterruptibleWait(sec);
            } break;
            case MacroOp::LABEL: break;
            case MacroOp::GOTO:
                if (op.a == 0 || gotoLeft[pc] > 0) {
                    if (op.a > 0) gotoLeft[pc]--;
                    pc = (size_t)op.jump;
                    continue;
                }
                break;
            case MacroOp::WAIT_PIXEL: {
                ULONGLONG t0 = GetTickCount64(); bool ok = false;
                while (g_IsPlaying && GetTickCount64() - t0 < (ULONGLONG)(op.f1 * 1000)) {
                    if (PixelMatch(op.a, op.b, op.c, op.d)) { ok = true; break; }
                    InterruptibleWait(0.5f);
                }
                if (!ok) LogMsg("wait_pixel (%d,%d) 超时 %.1fs, 继续执行", op.a, op.b, op.f1);
            } break;
            case MacroOp::IF_PIXEL:
                if (PixelMatch(op.a, op.b, op.c, op.d)) { pc = (size_t)op.jump; continue; }
                break;
            case MacroOp::CLICK_IMAGE: {
                Img t;
                if (!LoadTemplateFile(op.s1.c_str(), t)) { LogMsg("click_image: 模板 %s 加载失败", op.s1.c_str()); break; }
                ULONGLONG t0 = GetTickCount64(); bool ok = false;
                while (g_IsPlaying && GetTickCount64() - t0 < (ULONGLONG)(op.f2 * 1000)) {
                    int x, y; double sim;
                    if (FindImageOnScreen(t, x, y, sim) && sim >= op.f1) {
                        Swipe(x, y, x, y, 100);
                        LogMsg("click_image: 命中 %s @(%d,%d) 相似度 %.3f", op.s1.c_str(), x, y, sim);
                        ok = true; break;
                    }
                    InterruptibleWait(1.0f);
                }
                if (!ok) LogMsg("click_image: %s 超时 %.1fs 未找到", op.s1.c_str(), op.f2);
            } break;
            case MacroOp::WAIT_TEXT: {
                ULONGLONG t0 = GetTickCount64(); bool ok = false;
                while (g_IsPlaying && GetTickCount64() - t0 < (ULONGLONG)(op.f1 * 1000)) {
                    std::string txt = RunOcr();
                    if (txt.find(op.s1) != std::string::npos) { ok = true; break; }
                    InterruptibleWait(2.0f);
                }
                if (ok) LogMsg("wait_text: 检测到 \"%s\"", op.s1.c_str());
                else LogMsg("wait_text: \"%s\" 超时 %.1fs, 继续执行", op.s1.c_str(), op.f1);
            } break;
            case MacroOp::STOP: g_IsPlaying = false; break;
            }
            pc++;
        }
        done++; g_PlayedLoops = done;
    }
    g_IsPlaying = false;
    LogMsg("脚本结束, 共完成 %d 轮", done);
}

// 启动执行 (返回 false = 已有脚本在运行或程序无效)
inline bool StartMacroProgram(const MacroProgram& prog, int loops) {
    if (g_IsPlaying || !prog.error.empty() || prog.ops.empty()) return false;
    g_IsPlaying = true; g_PlayedLoops = 0;
    MacroProgram copy = prog;
    std::thread([copy, loops]() { RunProgramBody(copy, loops); }).detach();
    return true;
}

inline bool StartMacroFile(const char* path, int loops) {
    MacroProgram prog;
    if (!ProgramFromFile(path, prog)) { LogMsg("脚本加载失败 %s: %s", path, prog.error.c_str()); return false; }
    LogMsg("从文件启动脚本: %s", path);
    return StartMacroProgram(prog, loops);
}

inline bool StartMacroVector(int loops) {
    MacroProgram prog;
    {
        std::lock_guard<std::mutex> lk(g_ScriptMutex);
        if (g_Script.empty()) return false;
        VectorToProgram(g_Script, prog);
    }
    return StartMacroProgram(prog, loops);
}
