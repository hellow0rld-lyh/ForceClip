/*
 * Clipboard Typer v1.0 — 调试版
 * ================================
 * 每次触发都会写日志到桌面 debug_ct.txt
 * 方便排查热键 / 剪贴板 / 打字哪一步没反应
 */

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>

#include <string>
#include <vector>
#include <fstream>
#include <ctime>
#include <algorithm>
#include <shlobj.h>

// ── 日志 ────────────────────────────────────────────────────────────────
static void log_msg(const char *msg) {
    // 写到用户桌面，方便查看
    wchar_t path[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_DESKTOP, nullptr, 0, path);
    wcscat_s(path, L"\\debug_ct.txt");

    std::wofstream log(path, std::ios::app);
    if (log) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        log << L"[" << st.wHour << L":" << st.wMinute << L":" << st.wSecond
            << L"." << st.wMilliseconds << L"] "
            << (msg ? std::wstring(msg, msg + strlen(msg)).c_str() : L"null")
            << std::endl;
    }
}

static void log_wmsg(const std::wstring &msg) {
    wchar_t path[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_DESKTOP, nullptr, 0, path);
    wcscat_s(path, L"\\debug_ct.txt");

    std::wofstream log(path, std::ios::app);
    if (log) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        log << L"[" << st.wHour << L":" << st.wMinute << L":" << st.wSecond
            << L"." << st.wMilliseconds << L"] " << msg << std::endl;
    }
}

// ── 模拟打字：用 VkKeyScanW + 虚拟键码（兼容性更好） ──────────────────
static void simulate_typing(const std::wstring &text) {
    if (text.empty()) {
        log_msg("simulate_typing: text is empty");
        return;
    }

    log_msg("simulate_typing: building input events...");
    size_t len = std::min((size_t)50000, text.size());

    std::vector<INPUT> inputs;
    inputs.reserve(len * 4);  // shift down + key down + key up + shift up

    for (size_t i = 0; i < len; ++i) {
        wchar_t ch = text[i];

        switch (ch) {
            case L'\r': continue;
            case L'\n': {
                INPUT d = {}; d.type = INPUT_KEYBOARD; d.ki.wVk = VK_RETURN; inputs.push_back(d);
                INPUT u = {}; u.type = INPUT_KEYBOARD; u.ki.wVk = VK_RETURN; u.ki.dwFlags = KEYEVENTF_KEYUP; inputs.push_back(u);
                continue;
            }
            case L'\t': {
                INPUT d = {}; d.type = INPUT_KEYBOARD; d.ki.wVk = VK_TAB; inputs.push_back(d);
                INPUT u = {}; u.type = INPUT_KEYBOARD; u.ki.wVk = VK_TAB; u.ki.dwFlags = KEYEVENTF_KEYUP; inputs.push_back(u);
                continue;
            }
        }

        // 用 VkKeyScanW 把字符转成虚拟键码 + Shift 状态
        SHORT vkScan = VkKeyScanW(ch);
        if (vkScan == -1) {
            // 键盘布局里没有这个字符 → 退回到 KEYEVENTF_UNICODE
            INPUT d = {}; d.type = INPUT_KEYBOARD; d.ki.wScan = ch; d.ki.dwFlags = KEYEVENTF_UNICODE; inputs.push_back(d);
            INPUT u = {}; u.type = INPUT_KEYBOARD; u.ki.wScan = ch; u.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP; inputs.push_back(u);
            continue;
        }

        BYTE vk = LOBYTE(vkScan);
        BYTE shift = HIBYTE(vkScan) & 1;  // 是否需要 Shift

        if (shift) {
            INPUT sd = {}; sd.type = INPUT_KEYBOARD; sd.ki.wVk = VK_SHIFT; inputs.push_back(sd);
        }

        INPUT d = {}; d.type = INPUT_KEYBOARD; d.ki.wVk = vk; inputs.push_back(d);
        INPUT u = {}; u.type = INPUT_KEYBOARD; u.ki.wVk = vk; u.ki.dwFlags = KEYEVENTF_KEYUP; inputs.push_back(u);

        if (shift) {
            INPUT su = {}; su.type = INPUT_KEYBOARD; su.ki.wVk = VK_SHIFT; su.ki.dwFlags = KEYEVENTF_KEYUP; inputs.push_back(su);
        }
    }

    if (inputs.empty()) {
        log_msg("simulate_typing: no input events generated");
        return;
    }

    log_msg("simulate_typing: calling SendInput...");
    UINT sent = SendInput((UINT)inputs.size(), inputs.data(), sizeof(INPUT));
    log_wmsg(L"simulate_typing: SendInput returned " + std::to_wstring(sent)
             + L" / " + std::to_wstring(inputs.size()));

    if (sent != inputs.size()) {
        log_wmsg(L"Last error: " + std::to_wstring(GetLastError()));
    }
}

// ── 剪贴板读取 ──────────────────────────────────────────────────────────
static std::wstring read_clipboard() {
    if (!OpenClipboard(nullptr)) {
        DWORD err = GetLastError();
        log_wmsg(L"OpenClipboard failed, error=" + std::to_wstring(err));
        return {};
    }

    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) {
        log_msg("GetClipboardData: no CF_UNICODETEXT");
        CloseClipboard();
        return {};
    }

    auto *pText = (const wchar_t *)GlobalLock(hData);
    if (!pText) {
        log_msg("GlobalLock failed");
        CloseClipboard();
        return {};
    }

    std::wstring result(pText);
    GlobalUnlock(hData);
    CloseClipboard();

    log_wmsg(L"Clipboard read: " + std::to_wstring(result.size()) + L" chars");
    if (result.size() > 50) {
        log_wmsg(L"First 50 chars: " + result.substr(0, 50));
    } else {
        log_wmsg(L"Content: " + result);
    }

    if (result.size() > 50000) result.resize(50000);
    return result;
}

// ── 等待 Ctrl / Alt 释放 ────────────────────────────────────────────────
static void wait_modifier_release() {
    log_msg("waiting for modifiers to release...");
    int waited = 0;
    while (waited < 500) {
        if (!(GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
            !(GetAsyncKeyState(VK_MENU)    & 0x8000)) {
            log_msg("modifiers released");
            Sleep(50);
            return;
        }
        Sleep(10);
        waited += 10;
    }
    log_msg("modifiers timeout (still held), proceeding anyway");
    Sleep(50);
}

// ── 热键处理 ────────────────────────────────────────────────────────────
static void paste_clipboard() {
    log_msg("=== HOTKEY TRIGGERED ===");

    wait_modifier_release();

    std::wstring text = read_clipboard();
    if (text.empty()) {
        log_msg("clipboard empty or read failed — abort");
        return;
    }

    simulate_typing(text);
    log_msg("=== DONE ===");
}

// ── 窗口过程 ────────────────────────────────────────────────────────────
static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_HOTKEY:
            if (wParam == 1) {
                log_msg("WM_HOTKEY: Ctrl+Alt+P received");
                paste_clipboard();
                return 0;
            }
            if (wParam == 2) {
                log_msg("WM_HOTKEY: Ctrl+Alt+Q — exit");
                PostQuitMessage(0);
                return 0;
            }
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ── 入口 ────────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    // 清空旧日志
    wchar_t path[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_DESKTOP, nullptr, 0, path);
    wcscat_s(path, L"\\debug_ct.txt");
    DeleteFileW(path);

    log_msg("=== Clipboard Typer DEBUG started ===");

    WNDCLASS wc = {};
    wc.lpfnWndProc = window_proc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"ClipboardTyperDebugClass";

    if (!RegisterClass(&wc)) {
        log_msg("RegisterClass failed");
        return 1;
    }

    HWND hwnd = CreateWindowEx(0, L"ClipboardTyperDebugClass", L"CT",
                               WS_OVERLAPPEDWINDOW,
                               0, 0, 0, 0, nullptr, nullptr, hInst, nullptr);
    if (!hwnd) {
        log_msg("CreateWindowEx failed");
        return 1;
    }
    log_wmsg(L"Window created, hwnd=" + std::to_wstring((uintptr_t)hwnd));

    if (!RegisterHotKey(hwnd, 1, MOD_CONTROL | MOD_ALT, VK_INSERT)) {
        log_wmsg(L"RegisterHotKey Ctrl+Alt+P FAILED, error=" + std::to_wstring(GetLastError()));
        DestroyWindow(hwnd);
        return 1;
    }
    log_msg("RegisterHotKey Ctrl+Alt+P OK");

    if (!RegisterHotKey(hwnd, 2, MOD_CONTROL | MOD_ALT, 'Q')) {
        log_wmsg(L"RegisterHotKey Ctrl+Alt+Q FAILED, error=" + std::to_wstring(GetLastError()));
    } else {
        log_msg("RegisterHotKey Ctrl+Alt+Q OK");
    }

    log_msg("=== Ready. Waiting for hotkeys... ===");

    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    log_msg("=== Exited ===");
    return 0;
}
