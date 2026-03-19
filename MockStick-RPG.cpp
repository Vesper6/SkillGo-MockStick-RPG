#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <d3d11.h>
#include <vector>
#include <fstream>
#include <string>
#include <thread>
#include <atomic>
#include <tlhelp32.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#define IDI_ICON1 101

// --- 全局变量 ---
static int g_PhoneW = 1080, g_PhoneH = 2340;
static int g_JoyX = 540, g_JoyY = 1500, g_StepSize = 400;
static char g_CurrentSerial[64] = "等待扫描...";
static char g_ScriptFileName[128] = "rpg_macro_01.txt";

struct AdbAction { int x1, y1, x2, y2, ms; float delay; };
std::vector<AdbAction> g_Script;
std::atomic<bool> g_IsRecording{ false }, g_IsPlaying{ false };
ULONGLONG g_LastActionTime = 0;

HWND g_ScrcpyHwnd = NULL;
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static FILE* g_AdbPipe = nullptr;

// --- 基础功能函数 ---

std::string ScanAdbDevices() {
    FILE* pipe = _popen("adb devices", "r");
    if (!pipe) return "错误";
    char buffer[128];
    int line = 0; std::string res = "";
    while (fgets(buffer, sizeof(buffer), pipe)) {
        std::string s = buffer;
        size_t p = s.find("\tdevice");
        if (line > 0 && p != std::string::npos) { res = s.substr(0, p); break; }
        line++;
    }
    _pclose(pipe);
    return res.empty() ? "未发现设备" : res;
}

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

void RunAdb(const char* cmd) { if (g_AdbPipe) { fprintf(g_AdbPipe, "%s\n", cmd); fflush(g_AdbPipe); } }

void Swipe(int x1, int y1, int x2, int y2, int ms) {
    char c[128]; sprintf(c, "input swipe %d %d %d %d %d", x1, y1, x2, y2, ms);
    RunAdb(c);
    if (g_IsRecording) {
        float d = g_Script.empty() ? 0 : (float)(GetTickCount64() - g_LastActionTime) / 1000.0f;
        g_Script.push_back({ x1, y1, x2, y2, ms, d });
        g_LastActionTime = GetTickCount64();
    }
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
        SelectObject(hMem, hBm); PrintWindow(hw, hMem, PW_RENDERFULLCONTENT);
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
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

int main(int, char**) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), nullptr, nullptr, nullptr, nullptr, L"MockStickRPG", nullptr };
    wc.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1));
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"MockStick-RPG Pro v5.2", WS_OVERLAPPEDWINDOW, 100, 100, 2100, 1450, nullptr, nullptr, wc.hInstance, nullptr);

    DXGI_SWAP_CHAIN_DESC sd = {}; sd.BufferCount = 2; sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.OutputWindow = hwnd; sd.SampleDesc.Count = 1; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, nullptr, &g_pd3dDeviceContext);
    ID3D11Texture2D* pBB; g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBB)); g_pd3dDevice->CreateRenderTargetView(pBB, nullptr, &g_mainRenderTargetView); pBB->Release();

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    ImGui::CreateContext();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    ImGui::GetIO().Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", 22.0f, nullptr, ImGui::GetIO().Fonts->GetGlyphRangesChineseFull());

    strcpy(g_CurrentSerial, ScanAdbDevices().c_str());

    while (true) {
        MSG msg; while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); if (msg.message == WM_QUIT) goto end; }

        // --- 核心新增：WASD 键盘监听 ---
        // 仅当窗口获得焦点且非输入状态时响应，防止打字冲突
        if (GetForegroundWindow() == hwnd && !ImGui::GetIO().WantTextInput) {
            static ULONGLONG lastKeyTime = 0;
            if (GetTickCount64() - lastKeyTime > 200) { // 防止连发过快
                if (GetAsyncKeyState('W') & 0x8000) { Swipe(g_JoyX, g_JoyY, g_JoyX, g_JoyY - g_StepSize, 180); lastKeyTime = GetTickCount64(); }
                if (GetAsyncKeyState('S') & 0x8000) { Swipe(g_JoyX, g_JoyY, g_JoyX, g_JoyY + g_StepSize, 180); lastKeyTime = GetTickCount64(); }
                if (GetAsyncKeyState('A') & 0x8000) { Swipe(g_JoyX, g_JoyY, g_JoyX - g_StepSize, g_JoyY, 180); lastKeyTime = GetTickCount64(); }
                if (GetAsyncKeyState('D') & 0x8000) { Swipe(g_JoyX, g_JoyY, g_JoyX + g_StepSize, g_JoyY, 180); lastKeyTime = GetTickCount64(); }
            }
        }

        static ULONGLONG lastFind = 0;
        if ((!g_ScrcpyHwnd || !IsWindow(g_ScrcpyHwnd)) && GetTickCount64() - lastFind > 1500) { g_ScrcpyHwnd = FindScrcpyWindow(); lastFind = GetTickCount64(); }
        if (g_ScrcpyHwnd) g_Mirror.Update(g_ScrcpyHwnd, g_pd3dDevice, g_pd3dDeviceContext);

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

        // 1. 设备管理
        if (ImGui::CollapsingHeader("1. 设备管理", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::InputText("序列号", g_CurrentSerial, 64);
            float bw = ImGui::GetContentRegionAvail().x / 2.0f - 10;
            if (ImGui::Button("刷新设备列表", ImVec2(bw, 50))) strcpy(g_CurrentSerial, ScanAdbDevices().c_str());
            ImGui::SameLine();
            if (ImGui::Button("连接镜像窗口", ImVec2(bw, 50))) {
                system("taskkill /F /IM scrcpy.exe /T > nul 2>&1");
                Sleep(800);
                char cmd[256]; sprintf(cmd, "cmd /c scrcpy -s %s --no-audio --window-title \"MockStick_View\"", g_CurrentSerial);
                WinExec(cmd, SW_HIDE);
                if (g_AdbPipe) _pclose(g_AdbPipe);
                char adb[128]; sprintf(adb, "adb -s %s shell", g_CurrentSerial);
                g_AdbPipe = _popen(adb, "w");
            }
        }

        // 2. 宏指令脚本控制
        if (ImGui::CollapsingHeader("2. 宏指令脚本控制", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::InputText("脚本文件名", g_ScriptFileName, 128);
            ImGui::Text("内存指令步数: %d", (int)g_Script.size());
            float bw = ImGui::GetContentRegionAvail().x / 3.0f - 10;
            ImGui::BeginDisabled(g_IsRecording);
            if (ImGui::Button("载入脚本", ImVec2(bw, 40))) {
                std::ifstream ifs(g_ScriptFileName);
                if (ifs.is_open()) { g_Script.clear(); AdbAction a; while (ifs >> a.x1 >> a.y1 >> a.x2 >> a.y2 >> a.ms >> a.delay) g_Script.push_back(a); }
            }
            ImGui::SameLine();
            if (ImGui::Button("保存脚本", ImVec2(bw, 40))) {
                std::ofstream ofs(g_ScriptFileName);
                for (auto& a : g_Script) ofs << a.x1 << " " << a.y1 << " " << a.x2 << " " << a.y2 << " " << a.ms << " " << a.delay << "\n";
            }
            ImGui::SameLine();
            if (ImGui::Button("清空指令", ImVec2(bw, 40))) g_Script.clear();
            ImGui::EndDisabled();

            if (g_IsRecording) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
                if (ImGui::Button("停止录制", ImVec2(-1, 50))) g_IsRecording = false;
                ImGui::PopStyleColor();
            }
            else {
                if (ImGui::Button("开始录制", ImVec2(-1, 50))) { g_Script.clear(); g_IsRecording = true; g_LastActionTime = GetTickCount64(); }
            }

            if (!g_IsRecording && !g_Script.empty()) {
                if (ImGui::Button(g_IsPlaying ? "停止脚本" : "启动循环脚本", ImVec2(-1, 50))) {
                    if (g_IsPlaying) g_IsPlaying = false;
                    else std::thread([]() { g_IsPlaying = true; while (g_IsPlaying) { for (auto& a : g_Script) { if (!g_IsPlaying)break; Sleep((DWORD)(a.delay * 1000)); Swipe(a.x1, a.y1, a.x2, a.y2, a.ms); } } }).detach();
                }
            }
        }

        // 3. RPG虚拟摇杆
        if (ImGui::CollapsingHeader("3. RPG虚拟摇杆映射", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BulletText("物理键盘 WASD 已同步激活。");
            float cw = 160, ch = 80;
            ImGui::SetCursorPosX(480);
            if (ImGui::Button("向上 (W)", ImVec2(cw, ch))) Swipe(g_JoyX, g_JoyY, g_JoyX, g_JoyY - g_StepSize, 180);
            ImGui::SetCursorPosX(300);
            if (ImGui::Button("向左 (A)", ImVec2(cw, ch))) Swipe(g_JoyX, g_JoyY, g_JoyX - g_StepSize, g_JoyY, 180);
            ImGui::SameLine(); ImGui::Dummy(ImVec2(180, 0)); ImGui::SameLine();
            if (ImGui::Button("向右 (D)", ImVec2(cw, ch))) Swipe(g_JoyX, g_JoyY, g_JoyX + g_StepSize, g_JoyY, 180);
            ImGui::SetCursorPosX(480);
            if (ImGui::Button("向下 (S)", ImVec2(cw, ch))) Swipe(g_JoyX, g_JoyY, g_JoyX, g_JoyY + g_StepSize, 180);
        }

        // 4. 全局系统控制
        if (ImGui::CollapsingHeader("4. 全局系统控制", ImGuiTreeNodeFlags_DefaultOpen)) {
            float sw = ImGui::GetContentRegionAvail().x / 3.0f - 10;
            if (ImGui::Button("返回键", ImVec2(sw, 70))) RunAdb("input keyevent 4");
            ImGui::SameLine();
            if (ImGui::Button("音量加", ImVec2(sw, 70))) RunAdb("input keyevent 24");
            ImGui::SameLine();
            if (ImGui::Button("音量减", ImVec2(sw, 70))) RunAdb("input keyevent 25");
        }

        // 5. 使用说明书
        if (ImGui::CollapsingHeader("5. 使用说明书", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BulletText("一. 键盘 WASD 直接映射到摇杆中心。");
            ImGui::BulletText("二. 若键盘无效，请确保鼠标点击了一下本软件窗口。");
            ImGui::BulletText("三.脚本文件名支持手动修改，建议以 .txt 结尾以便识别。");
            ImGui::BulletText("四.清空列表按钮仅清空内存中的指令，不会删除已保存的文件。");
            ImGui::BulletText("五.录制模式下，载入、保存和清空功能将被锁定以保护数据。");
            ImGui::BulletText("六.执行脚本时，系统会根据录制时的时间差自动模拟真实的等待。");
        }

        ImGui::End();
        ImGui::Render();
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        float clr[4] = { 0.1f, 0.1f, 0.12f, 1.0f };
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }
end: return 0;
}