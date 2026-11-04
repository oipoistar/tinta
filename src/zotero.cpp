// Zotero's citation picker (zotero.h): two GETs to Better BibTeX on this
// machine, made with WinHTTP off the window's thread.

#pragma optimize("s", on)

#include <winsock2.h>  // before windows.h, which zotero.h brings
#include <ws2tcpip.h>

#include "zotero.h"
#include "app.h"

#include <winhttp.h>

#include <mutex>
#include <thread>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ws2_32.lib")

namespace zotero {

namespace {

std::mutex g_lock;
std::wstring g_base = L"http://127.0.0.1:23119";

// Something listens at the base address. Windows tries a refused
// connection again for two seconds, past WinHTTP's own timeouts, so a
// connection of its own, given `timeoutMs`, answers for it first
bool listening(int timeoutMs) {
    const std::wstring url = baseUrl();
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = (DWORD)-1;
    WSADATA data{};
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || WSAStartup(MAKEWORD(2, 2), &data)) return false;
    bool open = false;
    ADDRINFOW* found = nullptr;
    ADDRINFOW hints{};
    hints.ai_socktype = SOCK_STREAM;
    if (GetAddrInfoW(std::wstring(parts.lpszHostName, parts.dwHostNameLength).c_str(),
                     std::to_wstring(parts.nPort).c_str(), &hints, &found) == 0 && found) {
        SOCKET s = socket(found->ai_family, SOCK_STREAM, IPPROTO_TCP);
        u_long waitless = 1;
        if (s != INVALID_SOCKET && ioctlsocket(s, FIONBIO, &waitless) == 0) {
            connect(s, found->ai_addr, (int)found->ai_addrlen);
            fd_set connected, failed;
            FD_ZERO(&connected);
            FD_ZERO(&failed);
            FD_SET(s, &connected);
            FD_SET(s, &failed);
            timeval wait{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
            open = select(0, nullptr, &connected, &failed, &wait) == 1 && FD_ISSET(s, &connected);
        }
        if (s != INVALID_SOCKET) closesocket(s);
        FreeAddrInfoW(found);
    }
    WSACleanup();
    return open;
}

// A GET under the base address: true when it was answered within
// `timeoutMs` (0: however long the writer takes), with its status and body
bool get(const std::wstring& path, int timeoutMs, DWORD& status, std::string& body) {
    const std::wstring url = baseUrl() + path;
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) return false;
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    const std::wstring target = std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) +
                                std::wstring(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    HINTERNET session = WinHttpOpen(L"Tinta", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    // The probe gives up quickly; the picker waits on the writer, half an hour at most
    const int wait = timeoutMs > 0 ? timeoutMs : 2000;
    WinHttpSetTimeouts(session, wait, wait, wait, timeoutMs > 0 ? timeoutMs : 30 * 60 * 1000);
    HINTERNET connect = WinHttpConnect(session, host.c_str(), parts.nPort, 0);
    HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", target.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                     WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                     parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                                : nullptr;
    const bool answered = request &&
                          WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                          WinHttpReceiveResponse(request, nullptr);
    if (answered) {
        DWORD size = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &size, WINHTTP_NO_HEADER_INDEX);
        DWORD available = 0;
        while (WinHttpQueryDataAvailable(request, &available) && available > 0 && body.size() < (1u << 20)) {
            const size_t at = body.size();
            body.resize(at + available);
            DWORD got = 0;
            if (!WinHttpReadData(request, &body[at], available, &got) || got == 0) {
                body.resize(at);
                break;
            }
            body.resize(at + got);
        }
    }
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return answered;
}

std::string trimmed(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

}  // namespace

void setBaseUrl(const std::wstring& url) {
    std::lock_guard<std::mutex> lock(g_lock);
    g_base = url;
}

std::wstring baseUrl() {
    std::lock_guard<std::mutex> lock(g_lock);
    return g_base;
}

bool ready(int timeoutMs) {
    DWORD status = 0;
    std::string body;
    timeoutMs = std::max(1, timeoutMs);
    return listening(timeoutMs) && get(L"/better-bibtex/cayw?probe=true", timeoutMs, status, body) && status == 200 &&
           trimmed(body) == "ready";
}

bool pick(std::string& citation) {
    DWORD status = 0;
    std::string body;
    if (!get(L"/better-bibtex/cayw?format=pandoc&brackets=1", 0, status, body) || status != 200) return false;
    citation = trimmed(body);
    return true;
}

void ask(HWND hwnd, unsigned request) {
    std::thread([hwnd, request] {
        auto* answer = new Answer;
        if (ready()) answer->kind = pick(answer->citation) ? Answer::Picked : Answer::Failed;
        if (!PostMessageW(hwnd, WM_APP_ZOTERO, (WPARAM)request, (LPARAM)answer)) delete answer;
    }).detach();
}

}  // namespace zotero
