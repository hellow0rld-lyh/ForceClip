/*
 * ForceClip v2.0 — 完整 GUI 版
 * ====================================================
 * 功能： Ctrl+Alt+Insert 读取剪贴板 → 在当前光标处打字
 * 退出： Ctrl+Alt+Q
 * 托盘： 右键菜单 → 设置 / 退出
 * 设置： 可自定义快捷键（监听按键 / 手动输入），开机自启动
 *
 * 编译： g++ clipboard_typer.cpp -luser32 -lshell32 -o clipboard_typer.exe -O2 -s -mwindows
 */

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>   // Shell_NotifyIcon, SHGetFolderPathW
#include <shlobj.h>     // CSIDL_APPDATA
#include <winhttp.h>    // GitHub API 检测更新

#include <string>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <map>

#pragma comment(lib, "shell32.lib")

// ═════════════════════════════════════════════════════════════════════
//  常量
// ═════════════════════════════════════════════════════════════════════
constexpr UINT  WM_TRAYICON       = WM_APP + 1;
constexpr UINT  PASTE_HKID        = 1;
constexpr UINT  EXIT_HKID         = 2;
constexpr DWORD MAX_CHARS         = 50000;
constexpr DWORD KEY_WAIT_MS       = 10;
constexpr DWORD KEY_TIMEOUT_MS    = 500;
constexpr DWORD SETTLE_MS         = 50;

const wchar_t* APP_VERSION = L"1.1.0";
const wchar_t* REPO_OWNER  = L"hellow0rld-lyh";
const wchar_t* REPO_NAME   = L"autoInput";

// 控件 ID
enum CtrlId {
    IDC_PASTE_DISPLAY  = 101,
    IDC_PASTE_LISTEN   = 102,
    IDC_PASTE_CTRL     = 103,
    IDC_PASTE_ALT      = 104,
    IDC_PASTE_SHIFT    = 105,
    IDC_PASTE_WIN      = 106,
    IDC_PASTE_KEY      = 107,

    IDC_EXIT_DISPLAY   = 201,
    IDC_EXIT_LISTEN    = 202,
    IDC_EXIT_CTRL      = 203,
    IDC_EXIT_ALT       = 204,
    IDC_EXIT_SHIFT     = 205,
    IDC_EXIT_WIN       = 206,
    IDC_EXIT_KEY       = 207,

    IDC_AUTOSTART      = 300,
    IDC_APPLY          = 301,
    IDC_CHECKUPDATE    = 302,
};

// ═════════════════════════════════════════════════════════════════════
//  热键配置
// ═════════════════════════════════════════════════════════════════════
struct HotkeyBinding {
    UINT modifiers = 0;   // MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN
    UINT vk        = 0;   // 虚拟键码
};

static std::wstring hotkey_to_string(UINT mod, UINT vk) {
    std::wstring s;
    if (mod & MOD_CONTROL) s += L"Ctrl+";
    if (mod & MOD_ALT)     s += L"Alt+";
    if (mod & MOD_SHIFT)   s += L"Shift+";
    if (mod & MOD_WIN)     s += L"Win+";

    // VK 转名称
    static const std::map<UINT, const wchar_t*> names = {
        { VK_BACK,     L"Backspace" },  { VK_TAB,      L"Tab" },
        { VK_RETURN,   L"Enter" },      { VK_ESCAPE,   L"Escape" },
        { VK_SPACE,    L"Space" },      { VK_DELETE,   L"Delete" },
        { VK_HOME,     L"Home" },       { VK_END,      L"End" },
        { VK_PRIOR,    L"PageUp" },     { VK_NEXT,     L"PageDown" },
        { VK_INSERT,   L"Insert" },     { VK_SNAPSHOT, L"PrintScreen" },
        { VK_SCROLL,   L"ScrollLock" }, { VK_PAUSE,    L"Pause" },
        { VK_CAPITAL,  L"CapsLock" },   { VK_NUMLOCK,  L"NumLock" },
        { VK_LEFT,     L"Left" },       { VK_RIGHT,    L"Right" },
        { VK_UP,       L"Up" },         { VK_DOWN,     L"Down" },
        { VK_APPS,     L"Apps" },       { VK_SLEEP,    L"Sleep" },
        { VK_DIVIDE,   L"Divide" },     { VK_MULTIPLY, L"Multiply" },
        { VK_SUBTRACT, L"Subtract" },   { VK_ADD,      L"Add" },
        { VK_DECIMAL,  L"Decimal" },    { VK_SEPARATOR,L"Separator" },
        { VK_OEM_1,    L";" },          { VK_OEM_2,    L"/" },
        { VK_OEM_3,    L"`" },          { VK_OEM_4,    L"[" },
        { VK_OEM_5,    L"\\" },         { VK_OEM_6,    L"]" },
        { VK_OEM_7,    L"'" },          { VK_OEM_COMMA,L"," },
        { VK_OEM_PERIOD,L"." },         { VK_OEM_MINUS,L"-" },
        { VK_OEM_PLUS, L"=" },
    };

    auto it = names.find(vk);
    if (it != names.end()) {
        s += it->second;
    } else if (vk >= 'A' && vk <= 'Z') {
        s += (wchar_t)vk;
    } else if (vk >= '0' && vk <= '9') {
        s += (wchar_t)vk;
    } else if (vk >= VK_F1 && vk <= VK_F24) {
        s += L'F';
        s += std::to_wstring(vk - VK_F1 + 1);
    } else {
        wchar_t buf[32];
        swprintf(buf, 32, L"VK_%d", vk);
        s += buf;
    }
    return s;
}

// 字符串 → VK（用于 manual 输入解析）
static UINT string_to_vk(const std::wstring &name) {
    static const std::map<std::wstring, UINT> table = {
        { L"BACKSPACE", VK_BACK },  { L"BS", VK_BACK },
        { L"TAB",       VK_TAB },
        { L"ENTER",     VK_RETURN },  { L"RETURN", VK_RETURN },
        { L"ESCAPE",    VK_ESCAPE },  { L"ESC", VK_ESCAPE },
        { L"SPACE",     VK_SPACE },
        { L"DELETE",    VK_DELETE },  { L"DEL", VK_DELETE },
        { L"HOME",      VK_HOME },
        { L"END",       VK_END },
        { L"PAGEUP",    VK_PRIOR },   { L"PGUP", VK_PRIOR },
        { L"PAGEDOWN",  VK_NEXT },    { L"PGDN", VK_NEXT },
        { L"INSERT",    VK_INSERT },  { L"INS", VK_INSERT },
        { L"PRINTSCREEN", VK_SNAPSHOT }, { L"PRTSC", VK_SNAPSHOT },
        { L"SCROLLLOCK", VK_SCROLL },
        { L"PAUSE",     VK_PAUSE },
        { L"CAPSLOCK",  VK_CAPITAL },
        { L"NUMLOCK",   VK_NUMLOCK },
        { L"LEFT",      VK_LEFT },
        { L"RIGHT",     VK_RIGHT },
        { L"UP",        VK_UP },
        { L"DOWN",      VK_DOWN },
        { L"APPS",      VK_APPS },
        { L"SLEEP",     VK_SLEEP },
        { L"DIVIDE",    VK_DIVIDE },
        { L"MULTIPLY",  VK_MULTIPLY },
        { L"SUBTRACT",  VK_SUBTRACT },
        { L"ADD",       VK_ADD },
        { L"DECIMAL",   VK_DECIMAL },
        { L"SEPARATOR", VK_SEPARATOR },

        // F 键
        { L"F1",  VK_F1  },  { L"F2",  VK_F2  },
        { L"F3",  VK_F3  },  { L"F4",  VK_F4  },
        { L"F5",  VK_F5  },  { L"F6",  VK_F6  },
        { L"F7",  VK_F7  },  { L"F8",  VK_F8  },
        { L"F9",  VK_F9  },  { L"F10", VK_F10 },
        { L"F11", VK_F11 },  { L"F12", VK_F12 },
    };

    // A-Z
    if (name.size() == 1) {
        wchar_t c = name[0];
        if (c >= L'a' && c <= L'z') return (UINT)(c - 32);  // 转大写
        if (c >= L'A' && c <= L'Z') return (UINT)c;
        if (c >= L'0' && c <= L'9') return (UINT)c;
    }

    auto s = name;
    for (auto &ch : s) if (ch >= L'a' && ch <= L'z') ch = ch - 32;  // 转大写

    auto it = table.find(s);
    if (it != table.end()) return it->second;

    return 0;  // 未知
}

// ═════════════════════════════════════════════════════════════════════
//  注册表配置持久化
// ═════════════════════════════════════════════════════════════════════
static const wchar_t* REG_KEY = L"Software\\ForceClip";
static bool g_checkUpdate = true;  // 启动时检测更新

static void save_config(HotkeyBinding paste, HotkeyBinding exit, bool autoStart) {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, nullptr, 0,
                        KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"PasteModifiers", 0, REG_DWORD,
                       (BYTE*)&paste.modifiers, sizeof(DWORD));
        RegSetValueExW(hKey, L"PasteVK", 0, REG_DWORD,
                       (BYTE*)&paste.vk, sizeof(DWORD));
        RegSetValueExW(hKey, L"ExitModifiers", 0, REG_DWORD,
                       (BYTE*)&exit.modifiers, sizeof(DWORD));
        RegSetValueExW(hKey, L"ExitVK", 0, REG_DWORD,
                       (BYTE*)&exit.vk, sizeof(DWORD));
        DWORD val = autoStart ? 1 : 0;
        RegSetValueExW(hKey, L"AutoStart", 0, REG_DWORD,
                       (BYTE*)&val, sizeof(DWORD));
        val = g_checkUpdate ? 1 : 0;
        RegSetValueExW(hKey, L"CheckUpdate", 0, REG_DWORD,
                       (BYTE*)&val, sizeof(DWORD));
        RegCloseKey(hKey);
    }
}

static void load_config(HotkeyBinding &paste, HotkeyBinding &exit, bool &autoStart) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return;

    DWORD type, size;

    size = sizeof(DWORD);
    RegQueryValueExW(hKey, L"PasteModifiers", nullptr, &type, (BYTE*)&paste.modifiers, &size);
    size = sizeof(DWORD);
    RegQueryValueExW(hKey, L"PasteVK", nullptr, &type, (BYTE*)&paste.vk, &size);
    size = sizeof(DWORD);
    RegQueryValueExW(hKey, L"ExitModifiers", nullptr, &type, (BYTE*)&exit.modifiers, &size);
    size = sizeof(DWORD);
    RegQueryValueExW(hKey, L"ExitVK", nullptr, &type, (BYTE*)&exit.vk, &size);
    size = sizeof(DWORD);
    DWORD val = 0;
    RegQueryValueExW(hKey, L"AutoStart", nullptr, &type, (BYTE*)&val, &size);
    autoStart = (val != 0);

    size = sizeof(DWORD); val = 0;
    RegQueryValueExW(hKey, L"CheckUpdate", nullptr, &type, (BYTE*)&val, &size);
    g_checkUpdate = (val != 0);

    RegCloseKey(hKey);
}

// ═════════════════════════════════════════════════════════════════════
//  开机自启动
// ═════════════════════════════════════════════════════════════════════
static void set_auto_start(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
        return;

    if (enable) {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        RegSetValueExW(hKey, L"ForceClip", 0, REG_SZ,
                       (BYTE*)path, (DWORD)((wcslen(path) + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(hKey, L"ForceClip");
    }
    RegCloseKey(hKey);
}

static bool is_auto_start() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;

    DWORD type, size = 0;
    LONG ret = RegQueryValueExW(hKey, L"ForceClip", nullptr, &type, nullptr, &size);
    RegCloseKey(hKey);
    return (ret == ERROR_SUCCESS);
}

// ═════════════════════════════════════════════════════════════════════
//  全局状态
// ═════════════════════════════════════════════════════════════════════
static HWND          g_hwnd       = nullptr;  // 主隐藏窗口
static HWND          g_settingsWnd= nullptr;  // 设置窗口
static HINSTANCE     g_hInst      = nullptr;
static HotkeyBinding g_pasteHK;               // 当前粘贴热键
static HotkeyBinding g_exitHK;                // 当前退出热键
static NOTIFYICONDATAW g_nid = {};

// ═════════════════════════════════════════════════════════════════════
//  功能函数（剪贴板读取 + 打字）
// ═════════════════════════════════════════════════════════════════════
static void wait_modifier_release() noexcept {
    DWORD waited = 0;
    while (waited < KEY_TIMEOUT_MS) {
        if (!(GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
            !(GetAsyncKeyState(VK_MENU)   & 0x8000) &&
            !(GetAsyncKeyState(VK_SHIFT)  & 0x8000) &&
            !(GetAsyncKeyState(VK_LWIN)   & 0x8000) &&
            !(GetAsyncKeyState(VK_RWIN)   & 0x8000)) {
            break;
        }
        Sleep(KEY_WAIT_MS);
        waited += KEY_WAIT_MS;
    }
    Sleep(SETTLE_MS);
}

static void simulate_typing(const std::wstring &text) {
    if (text.empty()) return;

    size_t len = std::min<size_t>(text.size(), MAX_CHARS);
    std::vector<INPUT> inputs;
    inputs.reserve(len * 2 + 2);

    for (size_t i = 0; i < len; ++i) {
        wchar_t ch = text[i];
        WORD    vk  = 0;
        bool    special = true;

        switch (ch) {
            case L'\r': continue;
            case L'\n': vk = VK_RETURN; break;
            case L'\t': vk = VK_TAB;    break;
            case L'\b': vk = VK_BACK;   break;
            default:    special = false; break;
        }

        if (special) {
            INPUT d = {}; d.type = INPUT_KEYBOARD; d.ki.wVk = vk; inputs.push_back(d);
            INPUT u = {}; u.type = INPUT_KEYBOARD; u.ki.wVk = vk; u.ki.dwFlags = KEYEVENTF_KEYUP; inputs.push_back(u);
        } else {
            INPUT d = {}; d.type = INPUT_KEYBOARD; d.ki.wScan = ch; d.ki.dwFlags = KEYEVENTF_UNICODE; inputs.push_back(d);
            INPUT u = {}; u.type = INPUT_KEYBOARD; u.ki.wScan = ch; u.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP; inputs.push_back(u);
        }
    }

    if (!inputs.empty())
        SendInput((UINT)inputs.size(), inputs.data(), sizeof(INPUT));
}

static std::wstring read_clipboard() {
    if (!OpenClipboard(nullptr))
        throw std::runtime_error("OpenClipboard failed");

    struct Guard { ~Guard() { CloseClipboard(); } } guard;

    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) throw std::runtime_error("No CF_UNICODETEXT");

    auto *pText = (const wchar_t*)GlobalLock(hData);
    if (!pText) throw std::runtime_error("GlobalLock failed");

    std::wstring result(pText);
    GlobalUnlock(hData);

    if (result.size() > MAX_CHARS) result.resize(MAX_CHARS);
    return result;
}

static void paste_clipboard() {
    wait_modifier_release();
    try {
        auto text = read_clipboard();
        if (!text.empty()) simulate_typing(text);
    } catch (...) {}
}

// ═════════════════════════════════════════════════════════════════════
//  注册 / 注销热键
// ═════════════════════════════════════════════════════════════════════
static void apply_hotkeys(HWND hwnd) {
    UnregisterHotKey(hwnd, PASTE_HKID);
    UnregisterHotKey(hwnd, EXIT_HKID);

    if (g_pasteHK.vk && g_pasteHK.modifiers)
        RegisterHotKey(hwnd, PASTE_HKID, g_pasteHK.modifiers, g_pasteHK.vk);

    if (g_exitHK.vk && g_exitHK.modifiers)
        RegisterHotKey(hwnd, EXIT_HKID, g_exitHK.modifiers, g_exitHK.vk);
}

// ═════════════════════════════════════════════════════════════════════
//  托盘图标
// ═════════════════════════════════════════════════════════════════════
static void create_tray_icon(HWND hwnd) {
    g_nid = {};
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd   = hwnd;
    g_nid.uID    = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    wcscpy_s(g_nid.szTip, L"ForceClip");

    // 创建一个简单图标（16x16 纯色块）
    HDC hdc = GetDC(nullptr);
    HDC mem  = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, 16, 16);
    SelectObject(mem, bmp);
    RECT r = {0, 0, 16, 16};
    HBRUSH br = CreateSolidBrush(RGB(0, 120, 215));
    FillRect(mem, &r, br);
    DeleteObject(br);
    // 画个 "C" 字母
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(255, 255, 255));
    SelectObject(mem, GetStockObject(DEFAULT_GUI_FONT));
    DrawTextW(mem, L"C", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    ICONINFO ii = {TRUE, 0, 0, bmp, bmp};
    HICON hIcon = CreateIconIndirect(&ii);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, hdc);

    g_nid.hIcon = hIcon;
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    DestroyIcon(hIcon);
}

static void show_tray_menu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 100, L"设置 (S)");
    AppendMenuW(menu, MF_STRING, 102, L"检查更新 (U)");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 101, L"退出 (X)");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);

    if (cmd == 100) {
        // 打开设置窗口
        if (g_settingsWnd && IsWindow(g_settingsWnd)) {
            SetForegroundWindow(g_settingsWnd);
        } else {
            // 通过发送消息让主窗口创建设置
            SendMessageW(hwnd, WM_COMMAND, 1000, 0);
        }
    } else if (cmd == 101) {
        PostQuitMessage(0);
    } else if (cmd == 102) {
        SendMessageW(hwnd, WM_COMMAND, 1001, 0);  // 检查更新
    }
}

// ═════════════════════════════════════════════════════════════════════
//  检测更新
// ═════════════════════════════════════════════════════════════════════
struct UpdateInfo {
    std::wstring latestVersion;
    std::wstring releaseUrl;
    std::wstring releaseNotes;
    bool         available = false;
};

static bool check_for_update(UpdateInfo &info) {
    HINTERNET hSession = WinHttpOpen(L"ForceClip/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     nullptr, nullptr, 0);
    if (!hSession) return false;

    HINTERNET hConnect = WinHttpConnect(hSession, L"api.github.com",
                                        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

    wchar_t path[256];
    swprintf(path, 256, L"/repos/%s/%s/releases/latest", REPO_OWNER, REPO_NAME);

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path, nullptr,
                                            nullptr, nullptr, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

    WinHttpSetTimeouts(hRequest, 5000, 5000, 5000, 5000);

    bool ok = false;
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           nullptr, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, nullptr)) {

        std::string response;
        char buf[4096];
        DWORD read = 0;
        while (WinHttpReadData(hRequest, buf, sizeof(buf), &read) && read > 0) {
            response.append(buf, read);
        }

        // 解析 JSON — 简单字符串搜索
        auto find_val = [&](const char *key) -> std::string {
            auto p = response.find(key);
            if (p == std::string::npos) return "";
            p = response.find('"', p);
            if (p == std::string::npos) return "";
            auto e = response.find('"', p + 1);
            if (e == std::string::npos) return "";
            return response.substr(p + 1, e - p - 1);
        };

        std::string tag   = find_val("\"tag_name\"");
        std::string url   = find_val("\"html_url\"");

        if (!tag.empty() && !url.empty()) {
            info.latestVersion.assign(tag.begin(), tag.end());
            info.releaseUrl.assign(url.begin(), url.end());

            // 提取 release body（截取到下一个顶层 key 之前）
            auto body = response.find("\"body\"");
            if (body != std::string::npos) {
                body = response.find('"', body + 6);
                if (body != std::string::npos) {
                    auto end = response.find("\",", body + 1);
                    if (end != std::string::npos) {
                        info.releaseNotes.assign(
                            response.begin() + body + 1,
                            response.begin() + end);
                    }
                }
            }

            // 比较版本号
            std::wstring cur = APP_VERSION;
            if (cur[0] == L'v' || cur[0] == L'V') cur.erase(cur.begin());
            std::wstring lat = info.latestVersion;
            if (lat[0] == L'v' || lat[0] == L'V') lat.erase(lat.begin());

            // 简单比较：逐段比较 major.minor.patch
            auto parse = [](const std::wstring &s) -> int {
                return _wtoi(s.c_str());
            };
            auto dot1 = cur.find(L'.');
            auto dot2 = cur.rfind(L'.');
            int curMaj = parse(cur.substr(0, dot1));
            int curMin = parse(dot1 != std::wstring::npos ? cur.substr(dot1+1, dot2-dot1-1) : L"0");
            int curPat = parse(dot2 != std::wstring::npos ? cur.substr(dot2+1) : L"0");

            dot1 = lat.find(L'.');
            dot2 = lat.rfind(L'.');
            int latMaj = parse(lat.substr(0, dot1));
            int latMin = parse(dot1 != std::wstring::npos ? lat.substr(dot1+1, dot2-dot1-1) : L"0");
            int latPat = parse(dot2 != std::wstring::npos ? lat.substr(dot2+1) : L"0");

            info.available = (latMaj > curMaj) ||
                             (latMaj == curMaj && latMin > curMin) ||
                             (latMaj == curMaj && latMin == curMin && latPat > curPat);

            ok = true;
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ok;
}

static void show_update_dialog(HWND hwnd, const UpdateInfo &info) {
    if (!info.available) {
        std::wstring msg = L"当前版本: " + std::wstring(APP_VERSION) + L"\n已是最新版本。";
        MessageBoxW(hwnd, msg.c_str(), L"检查更新", MB_OK | MB_ICONINFORMATION);
        return;
    }

    std::wstring msg = L"发现新版本！\n\n"
        L"当前版本: " + std::wstring(APP_VERSION) + L"\n"
        L"最新版本: " + info.latestVersion + L"\n\n"
        L"更新内容:\n" + info.releaseNotes;

    int ret = MessageBoxW(hwnd, msg.c_str(), L"发现更新",
                          MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON1);
    if (ret == IDYES) {
        ShellExecuteW(nullptr, L"open", info.releaseUrl.c_str(),
                      nullptr, nullptr, SW_SHOWNORMAL);
    }
}

// ═════════════════════════════════════════════════════════════════════
//  设置窗口
// ═════════════════════════════════════════════════════════════════════
struct SettingsState {
    HWND hwnd = nullptr;
    bool capturing = false;          // 是否正在监听按键
    int  captureTarget = 0;          // 1=paste, 2=exit
    bool updating = false;           // 防止 EN_CHANGE 递归
    HotkeyBinding editPaste;
    HotkeyBinding editExit;
    bool editAutoStart = false;
};

static SettingsState g_ss;

// 更新设置窗口中的显示
static void update_settings_display() {
    if (g_ss.updating) return;       // 防止递归
    g_ss.updating = true;
    auto set_display = [](int id, const std::wstring &text) {
        SetDlgItemTextW(g_ss.hwnd, id, text.c_str());
    };
    set_display(IDC_PASTE_DISPLAY, hotkey_to_string(g_ss.editPaste.modifiers, g_ss.editPaste.vk));
    set_display(IDC_EXIT_DISPLAY,  hotkey_to_string(g_ss.editExit.modifiers,  g_ss.editExit.vk));

    // 复选框
    auto set_check = [](int id, bool checked) {
        SendDlgItemMessageW(g_ss.hwnd, id, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    };
    set_check(IDC_PASTE_CTRL,  g_ss.editPaste.modifiers & MOD_CONTROL);
    set_check(IDC_PASTE_ALT,   g_ss.editPaste.modifiers & MOD_ALT);
    set_check(IDC_PASTE_SHIFT, g_ss.editPaste.modifiers & MOD_SHIFT);
    set_check(IDC_PASTE_WIN,   g_ss.editPaste.modifiers & MOD_WIN);
    set_check(IDC_EXIT_CTRL,  g_ss.editExit.modifiers & MOD_CONTROL);
    set_check(IDC_EXIT_ALT,   g_ss.editExit.modifiers & MOD_ALT);
    set_check(IDC_EXIT_SHIFT, g_ss.editExit.modifiers & MOD_SHIFT);
    set_check(IDC_EXIT_WIN,   g_ss.editExit.modifiers & MOD_WIN);

    // 按键名
    auto set_key = [](int id, UINT vk) {
        SetDlgItemTextW(g_ss.hwnd, id, hotkey_to_string(0, vk).c_str());
    };
    set_key(IDC_PASTE_KEY, g_ss.editPaste.vk);
    set_key(IDC_EXIT_KEY,  g_ss.editExit.vk);

    SendDlgItemMessageW(g_ss.hwnd, IDC_AUTOSTART, BM_SETCHECK,
                        g_ss.editAutoStart ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(g_ss.hwnd, IDC_CHECKUPDATE, BM_SETCHECK,
                        g_checkUpdate ? BST_CHECKED : BST_UNCHECKED, 0);

    g_ss.updating = false;
}

// 从复选框 + key 编辑框读取手动设置的组合键
static void read_manual_to(HotkeyBinding &hk, int ctrlId, int altId, int shiftId, int winId, int keyId) {
    UINT mod = 0;
    if (SendDlgItemMessageW(g_ss.hwnd, ctrlId,  BM_GETCHECK, 0, 0) == BST_CHECKED) mod |= MOD_CONTROL;
    if (SendDlgItemMessageW(g_ss.hwnd, altId,   BM_GETCHECK, 0, 0) == BST_CHECKED) mod |= MOD_ALT;
    if (SendDlgItemMessageW(g_ss.hwnd, shiftId, BM_GETCHECK, 0, 0) == BST_CHECKED) mod |= MOD_SHIFT;
    if (SendDlgItemMessageW(g_ss.hwnd, winId,   BM_GETCHECK, 0, 0) == BST_CHECKED) mod |= MOD_WIN;
    hk.modifiers = mod;

    wchar_t buf[64];
    GetDlgItemTextW(g_ss.hwnd, keyId, buf, 64);
    UINT vk = string_to_vk(buf);
    if (vk == 0) vk = hk.vk;  // 保留旧值
    hk.vk = vk;
}

static void save_settings() {
    // 从界面读取
    read_manual_to(g_ss.editPaste,  IDC_PASTE_CTRL, IDC_PASTE_ALT, IDC_PASTE_SHIFT, IDC_PASTE_WIN, IDC_PASTE_KEY);
    read_manual_to(g_ss.editExit,   IDC_EXIT_CTRL,  IDC_EXIT_ALT,  IDC_EXIT_SHIFT,  IDC_EXIT_WIN,  IDC_EXIT_KEY);
    g_ss.editAutoStart = (SendDlgItemMessageW(g_ss.hwnd, IDC_AUTOSTART, BM_GETCHECK, 0, 0) == BST_CHECKED);
    g_checkUpdate = (SendDlgItemMessageW(g_ss.hwnd, IDC_CHECKUPDATE, BM_GETCHECK, 0, 0) == BST_CHECKED);

    // 更新全局
    g_pasteHK = g_ss.editPaste;
    g_exitHK  = g_ss.editExit;

    // 注册热键
    apply_hotkeys(g_hwnd);

    // 自启动
    set_auto_start(g_ss.editAutoStart);

    // 保存到注册表
    save_config(g_pasteHK, g_exitHK, g_ss.editAutoStart);

    update_settings_display();
}

static void cancel_settings() {
    g_ss.editPaste = g_pasteHK;
    g_ss.editExit  = g_exitHK;
    g_ss.editAutoStart = is_auto_start();
    update_settings_display();
}

// 设置窗口过程
static LRESULT CALLBACK settings_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG:  // 模拟，我们手动创建
            return 0;

        case WM_CLOSE: {
            cancel_settings();
            DestroyWindow(hwnd);
            g_ss.hwnd = nullptr;
            return 0;
        }

        case WM_COMMAND: {
            WORD id = LOWORD(wParam);
            WORD code = HIWORD(wParam);

            // 手动修改复选框或按键字段 → 更新显示
            if ((id >= IDC_PASTE_CTRL && id <= IDC_PASTE_KEY) ||
                (id >= IDC_EXIT_CTRL && id <= IDC_EXIT_KEY)) {
                if (code == BN_CLICKED || code == EN_CHANGE) {
                    if (id == IDC_PASTE_KEY || id == IDC_EXIT_KEY) {
                        // EN_CHANGE 会在每次按键触发，避免在 Edit 还没更新时反复解析
                        // 我们用 EN_UPDATE 或延迟处理
                    }
                    // 从手动输入刷新显示
                    read_manual_to(g_ss.editPaste,  IDC_PASTE_CTRL, IDC_PASTE_ALT, IDC_PASTE_SHIFT, IDC_PASTE_WIN, IDC_PASTE_KEY);
                    read_manual_to(g_ss.editExit,   IDC_EXIT_CTRL,  IDC_EXIT_ALT,  IDC_EXIT_SHIFT,  IDC_EXIT_WIN,  IDC_EXIT_KEY);
                    update_settings_display();
                }
                return 0;
            }

            switch (id) {
                case IDC_PASTE_LISTEN:
                case IDC_EXIT_LISTEN: {
                    // 进入监听模式
                    g_ss.capturing = true;
                    g_ss.captureTarget = (id == IDC_PASTE_LISTEN) ? 1 : 2;

                    // 修改按钮文字
                    SetDlgItemTextW(hwnd, id, L"按下按键...");

                    // 捕获键盘（不禁用任何控件，避免背景闪烁）
                    SetFocus(hwnd);
                    SetCapture(hwnd);
                    return 0;
                }

                case IDC_APPLY:
                    save_settings();
                    if (g_hwnd) SetForegroundWindow(g_hwnd);
                    return 0;

                case IDOK: {
                    save_settings();
                    DestroyWindow(hwnd);
                    g_ss.hwnd = nullptr;
                    return 0;
                }

                case IDCANCEL:
                    cancel_settings();
                    DestroyWindow(hwnd);
                    g_ss.hwnd = nullptr;
                    return 0;
            }
            return 0;
        }

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            if (g_ss.capturing) {
                UINT vk = (UINT)wParam;

                // 忽略纯修饰键
                if (vk == VK_CONTROL || vk == VK_MENU || vk == VK_SHIFT ||
                    vk == VK_LWIN || vk == VK_RWIN)
                    return 0;

                // 获取当前修饰键状态
                UINT mod = 0;
                if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mod |= MOD_CONTROL;
                if (GetAsyncKeyState(VK_MENU)    & 0x8000) mod |= MOD_ALT;
                if (GetAsyncKeyState(VK_SHIFT)   & 0x8000) mod |= MOD_SHIFT;
                if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) mod |= MOD_WIN;

                // 保存到编辑状态
                if (g_ss.captureTarget == 1) {
                    g_ss.editPaste.modifiers = mod;
                    g_ss.editPaste.vk = vk;
                } else {
                    g_ss.editExit.modifiers = mod;
                    g_ss.editExit.vk = vk;
                }

                // 释放键盘捕获
                ReleaseCapture();

                // 恢复按钮文字
                SetDlgItemTextW(hwnd, IDC_PASTE_LISTEN, L"监听");
                SetDlgItemTextW(hwnd, IDC_EXIT_LISTEN,  L"监听");

                // 更新界面
                update_settings_display();

                g_ss.capturing = false;
                return 0;
            }
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

static void create_settings_window(HINSTANCE hInst) {
    if (g_ss.hwnd && IsWindow(g_ss.hwnd)) {
        SetForegroundWindow(g_ss.hwnd);
        return;
    }

    // 用编辑状态的副本
    g_ss.editPaste    = g_pasteHK;
    g_ss.editExit     = g_exitHK;
    g_ss.editAutoStart = is_auto_start();
    g_ss.capturing    = false;

    const wchar_t CLASS_NAME[] = L"ForceClipSettingsClass";

    WNDCLASS wc = {};
    wc.lpfnWndProc   = settings_proc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);   // 标准对话框背景
    wc.lpszClassName = CLASS_NAME;
    RegisterClass(&wc);

    int W = 560, H = 300;
    int x = (GetSystemMetrics(SM_CXSCREEN) - W) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - H) / 2;

    g_ss.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME,
        CLASS_NAME, L"ForceClip - 设置",
        WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, W, H, g_hwnd, nullptr, hInst, nullptr);

    if (!g_ss.hwnd) return;

    // ── 创建控件 ────────────────────────────────────────────────────
    // 使用系统默认 GUI 字体，不自己创建（避免 DeleteObject 后崩溃）
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    auto label = [&](const wchar_t *text, int x, int y, int w, int h, int id) {
        HWND ctrl = CreateWindowExW(0, L"STATIC", text, WS_VISIBLE | WS_CHILD | SS_LEFT,
                                    x, y, w, h, g_ss.hwnd, (HMENU)(INT_PTR)id, hInst, nullptr);
        SendMessageW(ctrl, WM_SETFONT, (WPARAM)hFont, 0);
    };

    auto edit = [&](int x, int y, int w, int h, int id, bool readOnly) {
        DWORD style = WS_VISIBLE | WS_CHILD | WS_BORDER | ES_LEFT;
        if (readOnly) style |= ES_READONLY;
        HWND ctrl = CreateWindowExW(0, L"EDIT", L"", style,
                                    x, y, w, h, g_ss.hwnd, (HMENU)(INT_PTR)id, hInst, nullptr);
        SendMessageW(ctrl, WM_SETFONT, (WPARAM)hFont, 0);
        return ctrl;
    };

    auto button = [&](const wchar_t *text, int x, int y, int w, int h, int id) {
        HWND ctrl = CreateWindowExW(0, L"BUTTON", text, WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                    x, y, w, h, g_ss.hwnd, (HMENU)(INT_PTR)id, hInst, nullptr);
        SendMessageW(ctrl, WM_SETFONT, (WPARAM)hFont, 0);
    };

    auto checkbox = [&](const wchar_t *text, int x, int y, int w, int h, int id) {
        HWND ctrl = CreateWindowExW(0, L"BUTTON", text, WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
                                    x, y, w, h, g_ss.hwnd, (HMENU)(INT_PTR)id, hInst, nullptr);
        SendMessageW(ctrl, WM_SETFONT, (WPARAM)hFont, 0);
    };

    // ── 粘贴热键 ──
    auto group = [&](const wchar_t *text, int x, int y, int w, int h) {
        CreateWindowExW(0, L"BUTTON", text,
                        WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
                        x, y, w, h, g_ss.hwnd, nullptr, hInst, nullptr);
    };
    group(L"粘贴热键", 10, 5, 540, 85);

    label(L"当前:", 20, 25, 35, 20, 0);
    auto pasteDisp = edit(55, 22, 230, 22, IDC_PASTE_DISPLAY, true);
    button(L"监听", 295, 21, 70, 24, IDC_PASTE_LISTEN);

    label(L"修饰:", 20, 52, 32, 20, 0);
    checkbox(L"Ctrl",  55, 50, 50, 22, IDC_PASTE_CTRL);
    checkbox(L"Alt",  110, 50, 45, 22, IDC_PASTE_ALT);
    checkbox(L"Shift",160, 50, 55, 22, IDC_PASTE_SHIFT);
    checkbox(L"Win",  220, 50, 50, 22, IDC_PASTE_WIN);
    label(L"按键:", 275, 52, 35, 20, 0);
    edit(310, 50, 55, 22, IDC_PASTE_KEY, false);
    label(L"(手动填: A / F1 / Insert...)", 370, 52, 190, 20, 0);

    // ── 退出热键 ──
    group(L"退出热键", 10, 95, 540, 85);

    label(L"当前:", 20, 115, 35, 20, 0);
    auto exitDisp = edit(55, 112, 230, 22, IDC_EXIT_DISPLAY, true);
    button(L"监听", 295, 111, 70, 24, IDC_EXIT_LISTEN);

    label(L"修饰:", 20, 142, 32, 20, 0);
    checkbox(L"Ctrl",  55, 140, 50, 22, IDC_EXIT_CTRL);
    checkbox(L"Alt",  110, 140, 45, 22, IDC_EXIT_ALT);
    checkbox(L"Shift",160, 140, 55, 22, IDC_EXIT_SHIFT);
    checkbox(L"Win",  220, 140, 50, 22, IDC_EXIT_WIN);
    label(L"按键:", 275, 142, 35, 20, 0);
    edit(310, 140, 55, 22, IDC_EXIT_KEY, false);
    label(L"(手动填: Q / F3...)", 370, 142, 190, 20, 0);

    // ── 开机自启动 ──
    checkbox(L"开机自动启动", 18, 195, 150, 25, IDC_AUTOSTART);
    checkbox(L"启动时检查更新", 175, 195, 150, 25, IDC_CHECKUPDATE);

    // ── 底部按钮 ──
    button(L"确定",  280, 235, 65, 27, IDOK);
    button(L"取消",  355, 235, 65, 27, IDCANCEL);
    button(L"应用",  210, 235, 60, 27, IDC_APPLY);

    // 用设定值填充控件
    update_settings_display();
}

// ═════════════════════════════════════════════════════════════════════
//  主窗口过程
// ═════════════════════════════════════════════════════════════════════
static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_HOTKEY:
            if (wParam == PASTE_HKID) {
                paste_clipboard();
                return 0;
            }
            if (wParam == EXIT_HKID) {
                PostQuitMessage(0);
                return 0;
            }
            break;

        case WM_TRAYICON:
            if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
                show_tray_menu(hwnd);
                return 0;
            }
            break;

        case WM_COMMAND:
            if (wParam == 1000) {  // 打开设置
                create_settings_window(g_hInst);
                return 0;
            }
            if (wParam == 1001) {  // 检查更新
                UpdateInfo info;
                DWORD errCode = 0;
                if (check_for_update(info)) {
                    show_update_dialog(hwnd, info);
                } else {
                    errCode = GetLastError();
                    const wchar_t *errMsg;
                    switch (errCode) {
                        case 12002: errMsg = L"连接超时 (TIME_OUT)"; break;
                        case 12007: errMsg = L"无法解析服务器地址 (NAME_NOT_RESOLVED)"; break;
                        case 12029: errMsg = L"无法连接到服务器 (CONNECTION_REFUSED)"; break;
                        case 12030: errMsg = L"连接被断开 (CONNECTION_ABORTED)"; break;
                        case 12175: errMsg = L"SSL 安全连接失败 (SECURE_FAILURE)"; break;
                        default:   errMsg = L""; break;
                    }
                    wchar_t buf[256];
                    if (errMsg[0]) {
                        swprintf(buf, 256, L"检查更新失败\n\n%s\n\n请检查网络连接后重试。", errMsg);
                    } else {
                        swprintf(buf, 256, L"检查更新失败 (错误码: %u)\n\n请检查网络连接后重试。", errCode);
                    }
                    MessageBoxW(hwnd, buf, L"检查更新", MB_OK | MB_ICONWARNING);
                }
                return 0;
            }
            break;

        case WM_DESTROY:
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ═════════════════════════════════════════════════════════════════════
//  入口
// ═════════════════════════════════════════════════════════════════════
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    g_hInst = hInst;

    // ═══════════════════════════════════════════════════════════════
    //  单例检测 — 防止重复运行
    // ═══════════════════════════════════════════════════════════════
    const wchar_t *MUTEX_NAME = L"Local\\ForceClip_SingletonMutex";
    HANDLE hMutex = CreateMutexW(nullptr, FALSE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        hMutex = nullptr;

        HWND hExisting = FindWindowW(L"ForceClipClass", nullptr);

        int choice = MessageBoxW(nullptr,
            L"ForceClip 已在后台运行。\n\n"
            L"  [是]   显示已运行程序的设置窗口\n"
            L"  [否]   关闭旧程序并启动新实例\n"
            L"  [取消] 退出",
            L"ForceClip - 检测到重复运行",
            MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON3);

        if (choice == IDYES) {
            if (hExisting) {
                PostMessageW(hExisting, WM_COMMAND, 1000, 0);
                SetForegroundWindow(hExisting);
            }
            return 0;
        }

        if (choice == IDNO) {
            // 关闭旧进程
            DWORD pid = 0;
            if (hExisting) {
                GetWindowThreadProcessId(hExisting, &pid);
                PostMessageW(hExisting, WM_CLOSE, 0, 0);   // 请求优雅退出
                if (pid) {
                    HANDLE hProc = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pid);
                    if (hProc) {
                        // 等旧进程自己退出，最多 2 秒
                        if (WaitForSingleObject(hProc, 2000) == WAIT_TIMEOUT) {
                            TerminateProcess(hProc, 0);    // 超时则强制终止
                        }
                        CloseHandle(hProc);
                    }
                }
            }
            // 重新获取互斥锁
            hMutex = CreateMutexW(nullptr, FALSE, MUTEX_NAME);
            if (GetLastError() == ERROR_ALREADY_EXISTS) {
                MessageBoxW(nullptr, L"无法关闭旧进程，请手动结束后再试。", L"错误", MB_ICONERROR);
                if (hMutex) { CloseHandle(hMutex); hMutex = nullptr; }
                return 1;
            }
        } else {
            // 取消
            return 0;
        }
    }

    // 载入配置
    load_config(g_pasteHK, g_exitHK, g_ss.editAutoStart);

    // 默认值
    if (g_pasteHK.vk == 0) { g_pasteHK.modifiers = MOD_CONTROL | MOD_ALT; g_pasteHK.vk = VK_INSERT; }
    if (g_exitHK.vk  == 0) { g_exitHK.modifiers  = MOD_CONTROL | MOD_ALT; g_exitHK.vk  = 'Q'; }

    // 注册主窗口类
    constexpr auto CLASS_NAME = L"ForceClipClass";
    WNDCLASS wc = {};
    wc.lpfnWndProc = window_proc;
    wc.hInstance   = hInst;
    wc.lpszClassName = CLASS_NAME;
    if (!RegisterClass(&wc)) return 1;

    g_hwnd = CreateWindowExW(0, CLASS_NAME, L"ForceClip",
                             WS_OVERLAPPEDWINDOW,
                             0, 0, 0, 0, nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) return 1;

    // 热键
    apply_hotkeys(g_hwnd);

    // 托盘图标
    create_tray_icon(g_hwnd);

    // 如果没设置自启动但注册表有写入，同步一下
    if (g_ss.editAutoStart != is_auto_start())
        set_auto_start(g_ss.editAutoStart);

    // 启动时检测更新
    if (g_checkUpdate) {
        UpdateInfo info;
        if (check_for_update(info) && info.available) {
            show_update_dialog(g_hwnd, info);
        }
    }

    // 消息循环
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // 清理
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    UnregisterHotKey(g_hwnd, PASTE_HKID);
    UnregisterHotKey(g_hwnd, EXIT_HKID);
    DestroyWindow(g_hwnd);

    if (hMutex) CloseHandle(hMutex);
    return 0;
}
