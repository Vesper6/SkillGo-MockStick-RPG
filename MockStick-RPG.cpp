#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <d3d11.h>
#include <vector>
#include <fstream>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <cstdlib>
#include <ctime>
#include <tlhelp32.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ws2_32.lib")

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "mockstick/state.h"
#include "mockstick/util.h"
#include "mockstick/adb.h"
#include "mockstick/macro.h"
#include "mockstick/http_server.h"
#include "mockstick/config.h"

#define IDI_ICON1 101

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

HWND g_ScrcpyHwnd = NULL;
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

HWND FindScrcpyWindow() {
    DWORD pid = 0;
    HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe = { sizeof(pe) };
    if (Process32First(h, &pe)) {
        do { if (_stricmp(pe.szExeFile, "scrcpy.exe") == 0) { pid = pe.th32ProcessID; break; } } while (Process32Next(h, &pe));
    }
    CloseHandle(h);
    if (pid == 0) return NULL;
    struct Data { DWORD p; HWND hw; } d = { pid, NULL };
    EnumWindows([](HWND hw, LPARAM lp) -> BOOL {
        Data* p = (Data*)lp; DWORD wp; GetWindowThreadProcessId(hw, &wp);
        if (wp == p->p && GetParent(hw) == NULL && IsWindowVisible(hw)) { p->hw = hw; return FALSE; }
        return TRUE;
        }, (LPARAM)&d);
    return d.hw;
}

class StableMirror {
public:
    ID3D11ShaderResourceView* Texture = nullptr;
    ID3D11Texture2D* pTex = nullptr;
    void Update(HWND hw, ID3D11Device* dev, ID3D11DeviceContext* ctx) {
        if (!hw || !IsWindow(hw)) return;
        RECT r; GetWindowRect(hw, &r); int w = r.right - r.left, h = r.bottom - r.top;
        if (w < 100) return;
        HDC hSrc = GetDC(hw), hMem = CreateCompatibleDC(hSrc);
        HBITMAP hBm = CreateCompatibleBitmap(hSrc, w, h);
        HGDIOBJ hOld = SelectObject(hMem, hBm);
        PrintWindow(hw, hMem, PW_RENDERFULLCONTENT);
        // 必须先移出 DC 再 DeleteObject, 否则删除失败导致 GDI 句柄每帧泄漏
        SelectObject(hMem, hOld);
        BITMAPINFO bi = { 0 }; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        std::vector<DWORD> px(w * h); GetDIBits(hMem, hBm, 0, h, &px[0], &bi, DIB_RGB_COLORS);
        if (!Texture || mW != w || mH != h) {
            if (Texture) { Texture->Release(); pTex->Release(); }
            D3D11_TEXTURE2D_DESC d = { (UINT)w, (UINT)h, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1,0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
            dev->CreateTexture2D(&d, nullptr, &pTex); dev->CreateShaderResourceView(pTex, nullptr, &Texture);
            mW = w; mH = h;
        }
        ctx->UpdateSubresource(pTex, 0, nullptr, px.data(), w * 4, 0);
        DeleteObject(hBm); DeleteDC(hMem); ReleaseDC(hw, hSrc);
    }
private: int mW = 0, mH = 0;
};
StableMirror g_Mirror;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    if (msg == WM_SIZE && g_pd3dDevice && g_pSwapChain && wParam != SIZE_MINIMIZED) {
        if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
        g_pSwapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
        ID3D11Texture2D* pBB; g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBB));
        g_pd3dDevice->CreateRenderTargetView(pBB, nullptr, &g_mainRenderTargetView); pBB->Release();
        return 0;
    }
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// ---------- 各选项卡 UI ----------

static void TabDevices() {
    if (ImGui::Button("刷新设备列表", ImVec2(220, 40))) ScanDevices();
    ImGui::SameLine();
    if (ImGui::Button("连接选中设备", ImVec2(220, 40))) {
        ConnectDevices();
        std::string primary = PrimarySerial();
        if (!primary.empty()) {
            system("taskkill /F /IM scrcpy.exe /T > nul 2>&1");
            Sleep(800);
            char cmd[256]; snprintf(cmd, sizeof(cmd), "cmd /c scrcpy -s %s --no-audio --window-title \"MockStick_View\"", primary.c_str());
            WinExec(cmd, SW_HIDE);
            snprintf(g_StatusMsg, sizeof(g_StatusMsg), "已连接, 主设备: %s", primary.c_str());
        }
        else snprintf(g_StatusMsg, sizeof(g_StatusMsg), "没有勾选的设备");
    }
    ImGui::SameLine();
    if (ImGui::Button("断开全部", ImVec2(220, 40))) DisconnectDevices();

    if (ImGui::BeginTable("devs", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg, ImVec2(0, 220))) {
        ImGui::TableSetupColumn("广播", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("序列号");
        ImGui::TableSetupColumn("Shell 管道", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableHeadersRow();
        std::lock_guard<std::mutex> lk(g_DevMutex);
        int i = 0;
        for (auto& d : g_Devices) {
            ImGui::TableNextRow();
            ImGui::PushID(i++);
            ImGui::TableSetColumnIndex(0);
            ImGui::Checkbox("##sel", &d.selected);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(d.serial.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(d.pipe ? ImVec4(0.3f, 1.0f, 0.3f, 1) : ImVec4(0.7f, 0.7f, 0.7f, 1), d.pipe ? "已连接" : "未连接");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("勾选多台设备时, 摇杆/宏/HTTP 指令会同步广播到所有勾选设备; 第一台勾选设备为主设备 (镜像/截图/OCR)。");

    ImGui::Separator();
    ImGui::Text("全局系统控制:");
    float sw = ImGui::GetContentRegionAvail().x / 3.0f - 10;
    if (ImGui::Button("返回键", ImVec2(sw, 60))) RunAdb("input keyevent 4");
    ImGui::SameLine();
    if (ImGui::Button("音量加", ImVec2(sw, 60))) RunAdb("input keyevent 24");
    ImGui::SameLine();
    if (ImGui::Button("音量减", ImVec2(sw, 60))) RunAdb("input keyevent 25");
    if (g_StatusMsg[0]) ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", g_StatusMsg);
}

static void TabMacro() {
    ImGui::InputText("脚本文件名", g_ScriptFileName, 128);
    {
        std::lock_guard<std::mutex> lk(g_ScriptMutex);
        ImGui::Text("内存指令步数: %d", (int)g_Script.size());
    }
    float bw = ImGui::GetContentRegionAvail().x / 3.0f - 10;
    ImGui::BeginDisabled(g_IsRecording || g_IsPlaying);
    if (ImGui::Button("载入脚本", ImVec2(bw, 40))) {
        MacroProgram prog;
        if (!ProgramFromFile(g_ScriptFileName, prog)) {
            snprintf(g_StatusMsg, sizeof(g_StatusMsg), "载入失败: %s", prog.error.c_str());
        }
        else {
            std::lock_guard<std::mutex> lk(g_ScriptMutex);
            if (ProgramToVector(prog, g_Script)) snprintf(g_StatusMsg, sizeof(g_StatusMsg), "已载入 %d 步", (int)g_Script.size());
            else snprintf(g_StatusMsg, sizeof(g_StatusMsg), "该文件包含高级指令, 请到\"高级脚本\"区载入编辑");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("保存脚本", ImVec2(bw, 40))) {
        std::lock_guard<std::mutex> lk(g_ScriptMutex);
        SaveVectorAsV2(g_ScriptFileName, g_Script);
        snprintf(g_StatusMsg, sizeof(g_StatusMsg), "已保存到 %s (v2 格式)", g_ScriptFileName);
    }
    ImGui::SameLine();
    if (ImGui::Button("清空指令", ImVec2(bw, 40))) { std::lock_guard<std::mutex> lk(g_ScriptMutex); g_Script.clear(); }
    ImGui::EndDisabled();

    if (g_IsRecording) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
        if (ImGui::Button("停止录制", ImVec2(-1, 46))) { g_IsRecording = false; LogMsg("录制结束, 共 %d 步", (int)g_Script.size()); }
        ImGui::PopStyleColor();
    }
    else {
        ImGui::BeginDisabled(g_IsPlaying);
        if (ImGui::Button("开始录制", ImVec2(-1, 46))) {
            { std::lock_guard<std::mutex> lk(g_ScriptMutex); g_Script.clear(); }
            g_IsRecording = true; g_LastActionTime = GetTickCount64();
            LogMsg("开始录制");
        }
        ImGui::EndDisabled();
    }

    ImGui::SetNextItemWidth(180);
    ImGui::InputInt("循环次数 (0=无限)", &g_LoopCount);
    if (g_LoopCount < 0) g_LoopCount = 0;
    if (g_IsPlaying) { ImGui::SameLine(); ImGui::Text("已完成循环: %d", g_PlayedLoops.load()); }
    bool hasScript;
    { std::lock_guard<std::mutex> lk(g_ScriptMutex); hasScript = !g_Script.empty(); }
    ImGui::BeginDisabled(g_IsRecording || (!g_IsPlaying && !hasScript));
    if (ImGui::Button(g_IsPlaying ? "停止脚本" : "启动循环脚本", ImVec2(-1, 46))) {
        if (g_IsPlaying) g_IsPlaying = false;
        else StartMacroVector(g_LoopCount);
    }
    ImGui::EndDisabled();

    // --- 步骤编辑器 ---
    if (ImGui::CollapsingHeader("步骤编辑器")) {
        ImGui::BeginDisabled(g_IsRecording || g_IsPlaying);
        std::lock_guard<std::mutex> lk(g_ScriptMutex);
        int removeIdx = -1, upIdx = -1, downIdx = -1;
        if (ImGui::BeginTable("steps", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 320))) {
            const char* cols[] = { "#", "X1", "Y1", "X2", "Y2", "毫秒", "延迟(s)", "操作" };
            for (auto c : cols) ImGui::TableSetupColumn(c);
            ImGui::TableHeadersRow();
            for (int i = 0; i < (int)g_Script.size(); i++) {
                AdbAction& a = g_Script[i];
                ImGui::TableNextRow();
                ImGui::PushID(i);
                ImGui::TableSetColumnIndex(0); ImGui::Text("%d", i + 1);
                ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1); ImGui::InputInt("##x1", &a.x1, 0);
                ImGui::TableSetColumnIndex(2); ImGui::SetNextItemWidth(-1); ImGui::InputInt("##y1", &a.y1, 0);
                ImGui::TableSetColumnIndex(3); ImGui::SetNextItemWidth(-1); ImGui::InputInt("##x2", &a.x2, 0);
                ImGui::TableSetColumnIndex(4); ImGui::SetNextItemWidth(-1); ImGui::InputInt("##y2", &a.y2, 0);
                ImGui::TableSetColumnIndex(5); ImGui::SetNextItemWidth(-1); ImGui::InputInt("##ms", &a.ms, 0);
                ImGui::TableSetColumnIndex(6); ImGui::SetNextItemWidth(-1); ImGui::InputFloat("##d", &a.delay, 0, 0, "%.2f");
                ImGui::TableSetColumnIndex(7);
                if (ImGui::SmallButton("试")) Swipe(a.x1, a.y1, a.x2, a.y2, a.ms);
                ImGui::SameLine(); if (ImGui::SmallButton("删")) removeIdx = i;
                ImGui::SameLine(); if (ImGui::SmallButton("上") && i > 0) upIdx = i;
                ImGui::SameLine(); if (ImGui::SmallButton("下") && i + 1 < (int)g_Script.size()) downIdx = i;
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (removeIdx >= 0) g_Script.erase(g_Script.begin() + removeIdx);
        if (upIdx > 0) std::swap(g_Script[upIdx], g_Script[upIdx - 1]);
        if (downIdx >= 0 && downIdx + 1 < (int)g_Script.size()) std::swap(g_Script[downIdx], g_Script[downIdx + 1]);
        if (ImGui::Button("末尾插入一步 (屏幕中心点按)", ImVec2(-1, 32)))
            g_Script.push_back({ g_PhoneW / 2, g_PhoneH / 2, g_PhoneW / 2, g_PhoneH / 2, 100, 1.0f });
        ImGui::EndDisabled();
    }

    // --- 高级脚本 (DSL v2) ---
    if (ImGui::CollapsingHeader("高级脚本 (条件/找图/OCR)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::InputText("高级脚本文件", g_AdvFileName, 128);
        float aw = ImGui::GetContentRegionAvail().x / 4.0f - 10;
        if (ImGui::Button("载入", ImVec2(aw, 34))) {
            std::ifstream f(g_AdvFileName);
            if (f.is_open()) {
                std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                snprintf(g_AdvScript, sizeof(g_AdvScript), "%s", text.c_str());
                snprintf(g_AdvCheckMsg, sizeof(g_AdvCheckMsg), "已载入 %s", g_AdvFileName);
            }
            else snprintf(g_AdvCheckMsg, sizeof(g_AdvCheckMsg), "无法打开 %s", g_AdvFileName);
        }
        ImGui::SameLine();
        if (ImGui::Button("保存", ImVec2(aw, 34))) {
            std::ofstream f(g_AdvFileName);
            f << g_AdvScript;
            snprintf(g_AdvCheckMsg, sizeof(g_AdvCheckMsg), "已保存 %s", g_AdvFileName);
        }
        ImGui::SameLine();
        if (ImGui::Button("语法检查", ImVec2(aw, 34))) {
            MacroProgram prog;
            if (ParseMacro(g_AdvScript, prog)) snprintf(g_AdvCheckMsg, sizeof(g_AdvCheckMsg), "语法正确, 共 %d 条指令", (int)prog.ops.size());
            else snprintf(g_AdvCheckMsg, sizeof(g_AdvCheckMsg), "%s", prog.error.c_str());
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(g_IsRecording);
        if (ImGui::Button(g_IsPlaying ? "停止" : "运行", ImVec2(aw, 34))) {
            if (g_IsPlaying) g_IsPlaying = false;
            else {
                MacroProgram prog;
                if (ParseMacro(g_AdvScript, prog)) StartMacroProgram(prog, g_LoopCount);
                else snprintf(g_AdvCheckMsg, sizeof(g_AdvCheckMsg), "%s", prog.error.c_str());
            }
        }
        ImGui::EndDisabled();
        if (g_AdvCheckMsg[0]) ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", g_AdvCheckMsg);
        ImGui::InputTextMultiline("##adv", g_AdvScript, sizeof(g_AdvScript), ImVec2(-1, 260));
        ImGui::TextDisabled("指令: tap/swipe/key/text/wait/random_wait/label/goto/wait_pixel/if_pixel/click_image/wait_text/stop, # 开头为注释");
    }
    if (g_StatusMsg[0]) ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", g_StatusMsg);
}

static void TabJoystick() {
    ImGui::BulletText("物理键盘 WASD 已同步激活, 支持对角线组合键。");
    float cw = 160, ch = 80;
    ImGui::SetCursorPosX(480);
    if (ImGui::Button("向上 (W)", ImVec2(cw, ch))) Swipe(g_JoyX, g_JoyY, g_JoyX, g_JoyY - g_StepSize, g_SwipeMs);
    ImGui::SetCursorPosX(300);
    if (ImGui::Button("向左 (A)", ImVec2(cw, ch))) Swipe(g_JoyX, g_JoyY, g_JoyX - g_StepSize, g_JoyY, g_SwipeMs);
    ImGui::SameLine(); ImGui::Dummy(ImVec2(180, 0)); ImGui::SameLine();
    if (ImGui::Button("向右 (D)", ImVec2(cw, ch))) Swipe(g_JoyX, g_JoyY, g_JoyX + g_StepSize, g_JoyY, g_SwipeMs);
    ImGui::SetCursorPosX(480);
    if (ImGui::Button("向下 (S)", ImVec2(cw, ch))) Swipe(g_JoyX, g_JoyY, g_JoyX, g_JoyY + g_StepSize, g_SwipeMs);

    ImGui::Separator();
    ImGui::Text("参数设置:");
    ImGui::SetNextItemWidth(200); ImGui::InputInt("手机分辨率 宽", &g_PhoneW); ImGui::SameLine();
    ImGui::SetNextItemWidth(200); ImGui::InputInt("高", &g_PhoneH);
    ImGui::SetNextItemWidth(200); ImGui::InputInt("摇杆中心 X", &g_JoyX); ImGui::SameLine();
    ImGui::SetNextItemWidth(200); ImGui::InputInt("Y", &g_JoyY);
    ImGui::SliderInt("滑动步长 (px)", &g_StepSize, 50, 1000);
    ImGui::SliderInt("滑动时长 (ms)", &g_SwipeMs, 50, 1000);
    if (g_PhoneW < 1) g_PhoneW = 1;
    if (g_PhoneH < 1) g_PhoneH = 1;
    if (ImGui::Button("保存参数配置", ImVec2(-1, 40))) { SaveConfig(); snprintf(g_StatusMsg, sizeof(g_StatusMsg), "参数已保存到 %s", kConfigFile); }
}

static void TabAutomation() {
    ImGui::Text("HTTP API / MCP 接入");
    ImGui::TextColored(g_HttpRun ? ImVec4(0.3f, 1.0f, 0.3f, 1) : ImVec4(1.0f, 0.5f, 0.3f, 1),
        g_HttpRun ? "运行中: http://127.0.0.1:%d" : "未运行 (端口 %d)", g_HttpPort);
    ImGui::SetNextItemWidth(160);
    ImGui::InputInt("端口", &g_HttpPort);
    ImGui::SameLine();
    ImGui::Checkbox("开机自启", &g_HttpEnabled);
    ImGui::SameLine();
    if (ImGui::Button(g_HttpRun ? "停止服务" : "启动服务", ImVec2(160, 30))) {
        if (g_HttpRun) StopHttpServer();
        else StartHttpServer();
    }
    ImGui::TextDisabled("接口: /tap /swipe /key /text /screenshot /pixel /macro/run /macro/stop /uidump /ocr /find_image /save_region /status");
    ImGui::TextDisabled("MCP: 运行 mcp/mockstick_mcp.py 即可让 Claude 等 AI 通过看图+操作控制手机, 详见 mcp/README.md");

    ImGui::Separator();
    ImGui::Text("定时任务 (到点自动唤醒屏幕并执行脚本, 每天一次)");
    int schedRemove = -1;
    if (ImGui::BeginTable("sched", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg, ImVec2(0, 180))) {
        ImGui::TableSetupColumn("启用", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("时", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("分", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("脚本文件");
        ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableHeadersRow();
        for (int i = 0; i < (int)g_Sched.size(); i++) {
            SchedTask& t = g_Sched[i];
            ImGui::TableNextRow();
            ImGui::PushID(1000 + i);
            ImGui::TableSetColumnIndex(0); ImGui::Checkbox("##en", &t.enabled);
            ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1); ImGui::InputInt("##h", &t.hour, 0);
            ImGui::TableSetColumnIndex(2); ImGui::SetNextItemWidth(-1); ImGui::InputInt("##m", &t.minute, 0);
            ImGui::TableSetColumnIndex(3); ImGui::SetNextItemWidth(-1); ImGui::InputText("##f", t.file, sizeof(t.file));
            ImGui::TableSetColumnIndex(4);
            if (ImGui::SmallButton("删除")) schedRemove = i;
            if (t.hour < 0) t.hour = 0; if (t.hour > 23) t.hour = 23;
            if (t.minute < 0) t.minute = 0; if (t.minute > 59) t.minute = 59;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (schedRemove >= 0) g_Sched.erase(g_Sched.begin() + schedRemove);
    if (ImGui::Button("添加定时任务", ImVec2(220, 32))) g_Sched.push_back(SchedTask{});
    ImGui::SameLine();
    if (ImGui::Button("保存定时配置", ImVec2(220, 32))) { SaveConfig(); snprintf(g_StatusMsg, sizeof(g_StatusMsg), "定时配置已保存"); }

    ImGui::Separator();
    ImGui::Text("找图模板工具 (从主设备当前画面截取)");
    static int tx = 0, ty = 0, tw = 100, th = 100;
    static char tmplFile[128] = "template01.mstr";
    ImGui::SetNextItemWidth(110); ImGui::InputInt("X##t", &tx, 0); ImGui::SameLine();
    ImGui::SetNextItemWidth(110); ImGui::InputInt("Y##t", &ty, 0); ImGui::SameLine();
    ImGui::SetNextItemWidth(110); ImGui::InputInt("宽##t", &tw, 0); ImGui::SameLine();
    ImGui::SetNextItemWidth(110); ImGui::InputInt("高##t", &th, 0);
    ImGui::SetNextItemWidth(300); ImGui::InputText("模板文件", tmplFile, sizeof(tmplFile));
    if (ImGui::Button("截取保存模板", ImVec2(220, 32))) {
        char file[128]; snprintf(file, sizeof(file), "%s", tmplFile);
        int x = tx, y = ty, w = tw, h = th;
        std::thread([=]() {
            if (SaveRegionFile(x, y, w, h, file)) LogMsg("模板已保存: %s (%dx%d)", file, w, h);
            else LogMsg("模板保存失败 (设备未连接或区域无效)");
            }).detach();
    }
    ImGui::SameLine();
    if (ImGui::Button("测试找图", ImVec2(220, 32))) {
        char file[128]; snprintf(file, sizeof(file), "%s", tmplFile);
        std::thread([=]() {
            Img t;
            if (!LoadTemplateFile(file, t)) { LogMsg("找图测试: 模板 %s 加载失败", file); return; }
            int x, y; double sim;
            if (FindImageOnScreen(t, x, y, sim)) LogMsg("找图测试: 最佳位置 (%d,%d) 相似度 %.3f", x, y, sim);
            else LogMsg("找图测试: 截图失败");
            }).detach();
    }
    ImGui::SameLine();
    if (ImGui::Button("测试 OCR", ImVec2(220, 32))) {
        std::thread([]() {
            std::string txt = RunOcr();
            LogMsg("OCR 结果: %s", txt.empty() ? "(空)" : txt.c_str());
            }).detach();
    }
}

static void TabLogs() {
    if (ImGui::Button("清空日志显示", ImVec2(200, 30))) {
        std::lock_guard<std::mutex> lk(g_LogMutex);
        g_LogLines.clear();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("完整日志保存在 logs/ 目录");
    ImGui::BeginChild("logview", ImVec2(0, 0), ImGuiChildFlags_Borders);
    {
        std::lock_guard<std::mutex> lk(g_LogMutex);
        for (auto& l : g_LogLines) ImGui::TextUnformatted(l.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

static void TabProfiles() {
    static std::vector<std::string> profiles = ListProfiles();
    static int sel = -1;
    ImGui::InputText("档案名", g_ProfileName, sizeof(g_ProfileName));
    if (ImGui::Button("保存当前配置为档案", ImVec2(260, 36))) {
        if (SaveProfile(g_ProfileName)) { profiles = ListProfiles(); snprintf(g_StatusMsg, sizeof(g_StatusMsg), "档案已保存: %s", g_ProfileName); }
    }
    ImGui::SameLine();
    if (ImGui::Button("刷新列表", ImVec2(160, 36))) profiles = ListProfiles();
    ImGui::Separator();
    ImGui::Text("已有档案 (按游戏/场景保存参数+定时配置):");
    for (int i = 0; i < (int)profiles.size(); i++) {
        ImGui::PushID(2000 + i);
        if (ImGui::Selectable(profiles[i].c_str(), sel == i, 0, ImVec2(300, 0))) sel = i;
        ImGui::SameLine(340);
        if (ImGui::SmallButton("载入")) {
            if (LoadProfile(profiles[i].c_str())) snprintf(g_StatusMsg, sizeof(g_StatusMsg), "已载入档案: %s", profiles[i].c_str());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("删除")) {
            DeleteProfile(profiles[i].c_str());
            profiles = ListProfiles();
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    if (g_StatusMsg[0]) ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", g_StatusMsg);
}

static void TabHelp() {
    ImGui::BulletText("一. 键盘 WASD 直接映射到摇杆中心, 组合按键可走对角线。");
    ImGui::BulletText("二. 若键盘无效，请确保鼠标点击了一下本软件窗口。");
    ImGui::BulletText("三. 简易脚本以 v2 文本格式保存, 可手工编辑; 旧版 6 数字格式仍可载入。");
    ImGui::BulletText("四. 高级脚本支持条件判断 (wait_pixel/if_pixel)、找图 (click_image)、OCR (wait_text)。");
    ImGui::BulletText("五. 录制或回放期间, 载入、保存和清空功能将被锁定以保护数据。");
    ImGui::BulletText("六. HTTP API 仅监听 127.0.0.1; 配合 mcp/mockstick_mcp.py 可让 AI 直接看屏操作。");
    ImGui::BulletText("七. 定时任务在软件运行期间生效, 到点自动唤醒屏幕执行指定脚本 (每天一次)。");
    ImGui::BulletText("八. 找图模板为自有 .mstr 格式, 用\"自动化\"页的截取工具生成。");
    ImGui::BulletText("九. OCR 依赖 Windows 10+ 自带识别引擎, 需保持 tools/ocr.ps1 与 exe 同目录层级。");
    ImGui::BulletText("十. 多设备: 勾选多台后指令同步广播, 适合多开搬砖。");
}

int main(int, char**) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    srand((unsigned)time(nullptr));
    LoadConfig();
    LogMsg("MockStick-RPG v6.0 启动");
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), nullptr, nullptr, nullptr, nullptr, L"MockStickRPG", nullptr };
    wc.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1));
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"MockStick-RPG Pro v6.0", WS_OVERLAPPEDWINDOW, 100, 100, 2100, 1450, nullptr, nullptr, wc.hInstance, nullptr);

    DXGI_SWAP_CHAIN_DESC sd = {}; sd.BufferCount = 2; sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.OutputWindow = hwnd; sd.SampleDesc.Count = 1; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, nullptr, &g_pd3dDeviceContext);
    if (FAILED(hr)) // 无独显/远程桌面等场景回退到软件渲染
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, nullptr, &g_pd3dDeviceContext);
    if (FAILED(hr)) { MessageBoxW(hwnd, L"D3D11 初始化失败", L"MockStick-RPG", MB_ICONERROR); return 1; }
    ID3D11Texture2D* pBB; g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBB)); g_pd3dDevice->CreateRenderTargetView(pBB, nullptr, &g_mainRenderTargetView); pBB->Release();

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    ImGui::CreateContext();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    // 按优先级探测系统中文字体, 全部缺失时退回 ImGui 默认字体保证可用
    const char* fontCandidates[] = { "C:\\Windows\\Fonts\\msyh.ttc", "C:\\Windows\\Fonts\\msyhbd.ttc", "C:\\Windows\\Fonts\\simhei.ttf", "C:\\Windows\\Fonts\\simsun.ttc" };
    for (const char* f : fontCandidates) {
        if (GetFileAttributesA(f) != INVALID_FILE_ATTRIBUTES) {
            ImGui::GetIO().Fonts->AddFontFromFileTTF(f, 22.0f, nullptr, ImGui::GetIO().Fonts->GetGlyphRangesChineseFull());
            break;
        }
    }

    ScanDevices();
    if (g_HttpEnabled) StartHttpServer();

    while (true) {
        MSG msg; while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); if (msg.message == WM_QUIT) goto end; }

        // --- WASD 键盘监听 (支持对角线组合键) ---
        // 仅当窗口获得焦点且非输入状态时响应，防止打字冲突
        if (GetForegroundWindow() == hwnd && !ImGui::GetIO().WantTextInput) {
            static ULONGLONG lastKeyTime = 0;
            if (GetTickCount64() - lastKeyTime > 200) { // 防止连发过快
                int dx = 0, dy = 0;
                if (GetAsyncKeyState('W') & 0x8000) dy -= 1;
                if (GetAsyncKeyState('S') & 0x8000) dy += 1;
                if (GetAsyncKeyState('A') & 0x8000) dx -= 1;
                if (GetAsyncKeyState('D') & 0x8000) dx += 1;
                if (dx || dy) {
                    float k = (dx && dy) ? 0.7071f : 1.0f; // 对角线归一化, 保持滑动距离一致
                    Swipe(g_JoyX, g_JoyY, g_JoyX + (int)(dx * g_StepSize * k), g_JoyY + (int)(dy * g_StepSize * k), g_SwipeMs);
                    lastKeyTime = GetTickCount64();
                }
            }
        }

        static ULONGLONG lastFind = 0;
        if ((!g_ScrcpyHwnd || !IsWindow(g_ScrcpyHwnd)) && GetTickCount64() - lastFind > 1500) { g_ScrcpyHwnd = FindScrcpyWindow(); lastFind = GetTickCount64(); }
        if (g_ScrcpyHwnd) g_Mirror.Update(g_ScrcpyHwnd, g_pd3dDevice, g_pd3dDeviceContext);

        SchedulerTick();

        ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();

        // [左侧：镜像]
        ImGui::SetNextWindowPos(ImVec2(30, 30)); ImGui::SetNextWindowSize(ImVec2(600, 1380));
        ImGui::Begin("实时同步镜像", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        float canvasW = ImGui::GetContentRegionAvail().x;
        float aspect = (float)g_PhoneH / g_PhoneW;
        ImVec2 sz = ImVec2(canvasW, canvasW * aspect);
        if (g_Mirror.Texture) ImGui::GetWindowDrawList()->AddImage((ImTextureID)g_Mirror.Texture, p0, ImVec2(p0.x + sz.x, p0.y + sz.y));
        else ImGui::Text("等待设备挂载...");
        ImGui::InvisibleButton("touch", sz);
        if (ImGui::IsItemHovered()) {
            static ImVec2 start;
            if (ImGui::IsMouseClicked(0)) start = ImGui::GetMousePos();
            if (ImGui::IsMouseReleased(0)) {
                ImVec2 end = ImGui::GetMousePos();
                auto M = [&](ImVec2 p) { return ImVec2((p.x - p0.x) / sz.x * g_PhoneW, (p.y - p0.y) / sz.y * g_PhoneH); };
                ImVec2 s = M(start), e = M(end);
                Swipe((int)s.x, (int)s.y, (int)e.x, (int)e.y, 150);
            }
        }
        ImGui::End();

        // [右侧：控制面板]
        ImGui::SetNextWindowPos(ImVec2(660, 30)); ImGui::SetNextWindowSize(ImVec2(1380, 1380));
        ImGui::Begin("MockStick 控制面板", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        if (ImGui::BeginTabBar("main_tabs")) {
            if (ImGui::BeginTabItem("设备")) { TabDevices(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("宏与脚本")) { TabMacro(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("摇杆")) { TabJoystick(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("自动化")) { TabAutomation(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("日志")) { TabLogs(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("档案")) { TabProfiles(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("说明")) { TabHelp(); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
        ImGui::End();

        ImGui::Render();
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        float clr[4] = { 0.1f, 0.1f, 0.12f, 1.0f };
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }
end:
    // 退出清理: 停线程与服务、存配置、断开 ADB、释放渲染资源
    g_IsPlaying = false; g_IsRecording = false;
    StopHttpServer();
    SaveConfig();
    DisconnectDevices();
    Sleep(100);
    ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
    if (g_Mirror.Texture) { g_Mirror.Texture->Release(); g_Mirror.pTex->Release(); }
    if (g_mainRenderTargetView) g_mainRenderTargetView->Release();
    if (g_pSwapChain) g_pSwapChain->Release();
    if (g_pd3dDeviceContext) g_pd3dDeviceContext->Release();
    if (g_pd3dDevice) g_pd3dDevice->Release();
    WSACleanup();
    return 0;
}
