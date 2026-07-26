// 内嵌 HTTP REST API (仅监听 127.0.0.1), 供脚本/MCP/外部工具远程控制
#pragma once
#include "state.h"
#include "util.h"
#include "adb.h"
#include "macro.h"
#include <thread>

inline SOCKET g_ListenSock = INVALID_SOCKET;

inline void HttpSend(SOCKET c, int code, const char* ctype, const std::string& body) {
    const char* st = code == 200 ? "OK" : (code == 404 ? "Not Found" : "Bad Request");
    char hdr[256];
    snprintf(hdr, sizeof(hdr), "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nConnection: close\r\nAccess-Control-Allow-Origin: *\r\n\r\n", code, st, ctype, (unsigned)body.size());
    send(c, hdr, (int)strlen(hdr), 0);
    size_t off = 0;
    while (off < body.size()) {
        int n = send(c, body.data() + off, (int)(body.size() - off), 0);
        if (n <= 0) break;
        off += n;
    }
}

inline void HttpOkJson(SOCKET c, const std::string& json) { HttpSend(c, 200, "application/json; charset=utf-8", json); }

inline void HttpHandleConn(SOCKET c) {
    std::string req;
    char buf[4096];
    // 全部参数经 query 传递, 读到头部结束即可
    while (req.find("\r\n\r\n") == std::string::npos) {
        int n = recv(c, buf, sizeof(buf), 0);
        if (n <= 0) break;
        req.append(buf, n);
        if (req.size() > 65536) break;
    }
    size_t sp1 = req.find(' '), sp2 = req.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) { closesocket(c); return; }
    std::string url = req.substr(sp1 + 1, sp2 - sp1 - 1);
    std::string path = url, query;
    size_t qm = url.find('?');
    if (qm != std::string::npos) { path = url.substr(0, qm); query = url.substr(qm + 1); }
    auto q = ParseQuery(query);

    if (path == "/status") {
        std::string devs;
        {
            std::lock_guard<std::mutex> lk(g_DevMutex);
            for (auto& d : g_Devices) {
                if (!devs.empty()) devs += ",";
                devs += "{\"serial\":\"" + JsonEscape(d.serial) + "\",\"selected\":" + (d.selected ? "true" : "false") + ",\"connected\":" + (d.pipe ? "true" : "false") + "}";
            }
        }
        char b[256];
        snprintf(b, sizeof(b), ",\"playing\":%s,\"recording\":%s,\"loops_done\":%d,\"phone_w\":%d,\"phone_h\":%d}",
            g_IsPlaying ? "true" : "false", g_IsRecording ? "true" : "false", g_PlayedLoops.load(), g_PhoneW, g_PhoneH);
        HttpOkJson(c, "{\"devices\":[" + devs + "]" + b);
    }
    else if (path == "/tap") {
        int x = QInt(q, "x", -1), y = QInt(q, "y", -1);
        if (x < 0 || y < 0) HttpSend(c, 400, "application/json", "{\"error\":\"need x,y\"}");
        else { Swipe(x, y, x, y, 100); HttpOkJson(c, "{\"ok\":true}"); }
    }
    else if (path == "/swipe") {
        int x1 = QInt(q, "x1", -1), y1 = QInt(q, "y1", -1), x2 = QInt(q, "x2", -1), y2 = QInt(q, "y2", -1), ms = QInt(q, "ms", 300);
        if (x1 < 0 || y1 < 0 || x2 < 0 || y2 < 0) HttpSend(c, 400, "application/json", "{\"error\":\"need x1,y1,x2,y2\"}");
        else { Swipe(x1, y1, x2, y2, ms); HttpOkJson(c, "{\"ok\":true}"); }
    }
    else if (path == "/key") {
        int code = QInt(q, "code", -1);
        if (code < 0) HttpSend(c, 400, "application/json", "{\"error\":\"need code\"}");
        else { char cmd[64]; snprintf(cmd, sizeof(cmd), "input keyevent %d", code); RunAdb(cmd); HttpOkJson(c, "{\"ok\":true}"); }
    }
    else if (path == "/text") {
        std::string s = QStr(q, "s", "");
        if (s.empty()) HttpSend(c, 400, "application/json", "{\"error\":\"need s\"}");
        else {
            std::string cmd = "input text ";
            for (char ch : s) { if (ch == ' ') cmd += "%s"; else cmd += ch; }
            RunAdb(cmd.c_str());
            HttpOkJson(c, "{\"ok\":true}");
        }
    }
    else if (path == "/screenshot") {
        std::string png;
        if (CapturePng(png)) HttpSend(c, 200, "image/png", png);
        else HttpSend(c, 400, "application/json", "{\"error\":\"capture failed, no device?\"}");
    }
    else if (path == "/pixel") {
        int x = QInt(q, "x", 0), y = QInt(q, "y", 0);
        std::vector<unsigned char> px; int w, h;
        if (CaptureRaw(px, w, h) && x >= 0 && y >= 0 && x < w && y < h) {
            const unsigned char* p = &px[((size_t)y * w + x) * 4];
            char b[96]; snprintf(b, sizeof(b), "{\"r\":%d,\"g\":%d,\"b\":%d}", p[0], p[1], p[2]);
            HttpOkJson(c, b);
        }
        else HttpSend(c, 400, "application/json", "{\"error\":\"capture failed or out of range\"}");
    }
    else if (path == "/macro/run") {
        std::string file = QStr(q, "file", "");
        int loops = QInt(q, "loops", 1);
        if (file.empty()) HttpSend(c, 400, "application/json", "{\"error\":\"need file\"}");
        else if (StartMacroFile(file.c_str(), loops)) HttpOkJson(c, "{\"ok\":true}");
        else HttpSend(c, 400, "application/json", "{\"error\":\"already playing or load failed\"}");
    }
    else if (path == "/macro/stop") {
        g_IsPlaying = false;
        HttpOkJson(c, "{\"ok\":true}");
    }
    else if (path == "/macro/status") {
        char b[96]; snprintf(b, sizeof(b), "{\"playing\":%s,\"loops_done\":%d}", g_IsPlaying ? "true" : "false", g_PlayedLoops.load());
        HttpOkJson(c, b);
    }
    else if (path == "/uidump") {
        std::string xml = UiDump();
        if (xml.empty()) HttpSend(c, 400, "application/json", "{\"error\":\"uidump failed\"}");
        else HttpSend(c, 200, "text/xml; charset=utf-8", xml);
    }
    else if (path == "/ocr") {
        std::string txt = RunOcr();
        HttpOkJson(c, "{\"text\":\"" + JsonEscape(txt) + "\"}");
    }
    else if (path == "/find_image") {
        std::string file = QStr(q, "file", "");
        float thresh = QFloat(q, "thresh", 0.9f);
        Img t; int x, y; double sim;
        if (file.empty() || !LoadTemplateFile(file.c_str(), t)) HttpSend(c, 400, "application/json", "{\"error\":\"template load failed\"}");
        else if (!FindImageOnScreen(t, x, y, sim)) HttpSend(c, 400, "application/json", "{\"error\":\"capture failed\"}");
        else {
            char b[128]; snprintf(b, sizeof(b), "{\"found\":%s,\"x\":%d,\"y\":%d,\"sim\":%.4f}", sim >= thresh ? "true" : "false", x, y, sim);
            HttpOkJson(c, b);
        }
    }
    else if (path == "/save_region") {
        int x = QInt(q, "x", 0), y = QInt(q, "y", 0), w = QInt(q, "w", 0), h = QInt(q, "h", 0);
        std::string file = QStr(q, "file", "");
        if (file.empty() || w <= 0 || h <= 0) HttpSend(c, 400, "application/json", "{\"error\":\"need x,y,w,h,file\"}");
        else if (SaveRegionFile(x, y, w, h, file.c_str())) HttpOkJson(c, "{\"ok\":true}");
        else HttpSend(c, 400, "application/json", "{\"error\":\"save failed\"}");
    }
    else HttpSend(c, 404, "application/json", "{\"error\":\"unknown endpoint\"}");
    closesocket(c);
}

inline void HttpServerThread() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { LogMsg("HTTP: WSAStartup 失败"); return; }
    g_ListenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_ListenSock == INVALID_SOCKET) { LogMsg("HTTP: socket 创建失败"); return; }
    BOOL yes = TRUE;
    setsockopt(g_ListenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // 仅本机可访问
    addr.sin_port = htons((u_short)g_HttpPort);
    if (bind(g_ListenSock, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(g_ListenSock, 8) != 0) {
        LogMsg("HTTP: 端口 %d 绑定失败 (被占用?)", g_HttpPort);
        closesocket(g_ListenSock); g_ListenSock = INVALID_SOCKET;
        return;
    }
    LogMsg("HTTP API 已启动: http://127.0.0.1:%d", g_HttpPort);
    while (g_HttpRun) {
        SOCKET c = accept(g_ListenSock, nullptr, nullptr);
        if (c == INVALID_SOCKET) break;
        std::thread(HttpHandleConn, c).detach();
    }
    if (g_ListenSock != INVALID_SOCKET) { closesocket(g_ListenSock); g_ListenSock = INVALID_SOCKET; }
    LogMsg("HTTP API 已停止");
}

inline void StartHttpServer() {
    if (g_HttpRun) return;
    g_HttpRun = true;
    std::thread(HttpServerThread).detach();
}

inline void StopHttpServer() {
    if (!g_HttpRun) return;
    g_HttpRun = false;
    if (g_ListenSock != INVALID_SOCKET) closesocket(g_ListenSock);  // 唤醒阻塞的 accept
}
