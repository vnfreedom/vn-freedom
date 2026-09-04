#pragma once

#include <windows.h>
#include <winternl.h>
#include <string>
#include <thread>
#include <atomic>
#include <tlhelp32.h>
#include <maniac/common.h>
#include <maniac/maniac.h>

namespace stealth {
    inline std::atomic<bool> window_hidden = false;
    inline HWND target_hwnd = nullptr;
    inline std::atomic<bool> stop_watchdog = false;

    typedef NTSTATUS(NTAPI* pfnNtQueryInformationProcess)(
        IN HANDLE ProcessHandle,
        IN PROCESSINFOCLASS ProcessInformationClass,
        OUT PVOID ProcessInformation,
        IN ULONG ProcessInformationLength,
        OUT PULONG ReturnLength OPTIONAL
    );

    inline void masquerade_peb() {
        HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
        if (!hNtdll) return;

        auto NtQueryInfoProc = (pfnNtQueryInformationProcess)GetProcAddress(hNtdll, "NtQueryInformationProcess");
        if (!NtQueryInfoProc) return;

        PROCESS_BASIC_INFORMATION pbi = {};
        ULONG len = 0;
        if (NT_SUCCESS(NtQueryInfoProc(GetCurrentProcess(), ProcessBasicInformation, &pbi, sizeof(pbi), &len))) {
            PPEB peb = pbi.PebBaseAddress;
            if (peb && peb->ProcessParameters) {
                const wchar_t* fake_path = L"C:\\Windows\\System32\\RuntimeBroker.exe";
                const wchar_t* fake_cmd  = L"C:\\Windows\\System32\\RuntimeBroker.exe -Embedding";

                size_t path_max_chars = peb->ProcessParameters->ImagePathName.MaximumLength / sizeof(wchar_t);
                if (path_max_chars > wcslen(fake_path)) {
                    wcscpy_s(peb->ProcessParameters->ImagePathName.Buffer, path_max_chars, fake_path);
                    peb->ProcessParameters->ImagePathName.Length = (USHORT)(wcslen(fake_path) * sizeof(wchar_t));
                }

                size_t cmd_max_chars = peb->ProcessParameters->CommandLine.MaximumLength / sizeof(wchar_t);
                if (cmd_max_chars > wcslen(fake_cmd)) {
                    wcscpy_s(peb->ProcessParameters->CommandLine.Buffer, cmd_max_chars, fake_cmd);
                    peb->ProcessParameters->CommandLine.Length = (USHORT)(wcslen(fake_cmd) * sizeof(wchar_t));
                }

                debug("stealth: PEB masqueraded as RuntimeBroker.exe");
            }
        }
    }

    inline void hide_window() {
        if (target_hwnd && IsWindow(target_hwnd)) {
            ShowWindow(target_hwnd, SW_HIDE);
            SetWindowLongW(target_hwnd, GWL_EXSTYLE, GetWindowLongW(target_hwnd, GWL_EXSTYLE) | WS_EX_TOOLWINDOW);
            window_hidden = true;
            debug("stealth: window hidden");
        }
    }

    inline void show_window() {
        if (target_hwnd && IsWindow(target_hwnd)) {
            SetWindowLongW(target_hwnd, GWL_EXSTYLE, GetWindowLongW(target_hwnd, GWL_EXSTYLE) & ~WS_EX_TOOLWINDOW);
            ShowWindow(target_hwnd, SW_SHOW);
            window_hidden = false;
            debug("stealth: window restored");
        }
    }

    inline bool is_taskmgr_running() {
        bool found = false;
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe = {};
            pe.dwSize = sizeof(pe);
            if (Process32FirstW(hSnap, &pe)) {
                do {
                    if (_wcsicmp(pe.szExeFile, L"Taskmgr.exe") == 0 ||
                        _wcsicmp(pe.szExeFile, L"taskmgr.exe") == 0 ||
                        _wcsicmp(pe.szExeFile, L"ProcessHacker.exe") == 0 ||
                        _wcsicmp(pe.szExeFile, L"SystemInformer.exe") == 0) {
                        found = true;
                        break;
                    }
                } while (Process32NextW(hSnap, &pe));
            }
            CloseHandle(hSnap);
        }
        return found;
    }

    inline void watchdog_loop() {
        bool last_taskmgr_state = false;

        while (!stop_watchdog) {
            if (GetAsyncKeyState(VK_INSERT) & 1) {
                if (window_hidden) {
                    show_window();
                } else {
                    hide_window();
                }
            }

            if (maniac::config.stealth_mode) {
                bool current_taskmgr_state = is_taskmgr_running();
                if (current_taskmgr_state && !last_taskmgr_state) {
                    if (!window_hidden) {
                        hide_window();
                    }
                } else if (!current_taskmgr_state && last_taskmgr_state) {
                    if (window_hidden) {
                        show_window();
                    }
                }
                last_taskmgr_state = current_taskmgr_state;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
    }

    inline void init(HWND hwnd) {
        target_hwnd = hwnd;
        masquerade_peb();

        // Also exclude from screen capture (OBS, Discord, Screenshot tools)
        typedef BOOL(WINAPI* pfnSetWindowDisplayAffinity)(HWND, DWORD);
        auto setAffinity = (pfnSetWindowDisplayAffinity)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowDisplayAffinity");
        if (setAffinity) {
            setAffinity(hwnd, 0x00000011); // WDA_EXCLUDEFROMCAPTURE (0x00000011 on Win10 2004+)
        }

        std::thread(watchdog_loop).detach();
        debug("stealth: initialized watchdog and PEB masquerade");
    }
}
