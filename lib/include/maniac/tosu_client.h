#pragma once

#include <string>
#include <stdexcept>
#include <maniac/common.h>
#include <windows.h>
#include <winhttp.h>

// Simple synchronous HTTP GET via WinHTTP — no extra deps needed.
// Returns response body as string, throws on any error.
inline std::string winhttp_get(const wchar_t *host, INTERNET_PORT port, const wchar_t *path) {
    HINTERNET session = WinHttpOpen(L"maniac/1.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) throw std::runtime_error("WinHTTP: failed to open session");

    HINTERNET connect = WinHttpConnect(session, host, port, 0);
    if (!connect) { WinHttpCloseHandle(session); throw std::runtime_error("WinHTTP: failed to connect"); }

    HINTERNET request = WinHttpOpenRequest(connect, L"GET", path,
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!request) { WinHttpCloseHandle(connect); WinHttpCloseHandle(session); throw std::runtime_error("WinHTTP: failed to open request"); }

    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr)) {
        WinHttpCloseHandle(request); WinHttpCloseHandle(connect); WinHttpCloseHandle(session);
        throw std::runtime_error("WinHTTP: request failed");
    }

    std::string body;
    DWORD available = 0;
    while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
        std::string chunk(available, '\0');
        DWORD read = 0;
        WinHttpReadData(request, chunk.data(), available, &read);
        body.append(chunk, 0, read);
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return body;
}

namespace tosu {
    constexpr INTERNET_PORT PORT = 24050;

    // Fetch the raw JSON from tosu's v1 endpoint.
    // Throws if tosu is not running or request fails.
    inline std::string fetch_json() {
        return winhttp_get(L"127.0.0.1", PORT, L"/json");
    }

    // Returns true if tosu is reachable (i.e. running).
    inline bool is_running() {
        try {
            fetch_json();
            return true;
        } catch (...) {
            return false;
        }
    }
}
