#include "keepalive.h"
#include "startup.h"
#include <shellapi.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <map>
#include <memory>

namespace {
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT_PTR kTimer = 1;
constexpr UINT kShow = 100, kPause = 101, kExit = 102, kHide = 103;
constexpr UINT kForcePause = 104, kForceResume = 105;
constexpr UINT kStartupNone = 110, kStartupWindows = 111, kStartupQQ = 112;
constexpr ULONGLONG kIntervalMs = 30000;
const wchar_t* kTitle = L"坦克风暴 · 后台保活助手";
const wchar_t* kWindowClass = L"TankStormKeepAliveStatus";
struct Counts { unsigned long long queued = 0, failed = 0; };
std::map<HWND, Counts> counts;
std::vector<tank::Candidate> targets;
HWND window = nullptr;
HICON activeIcon = nullptr, pausedIcon = nullptr;
NOTIFYICONDATAW tray{};
bool paused = false, alternate = false;
bool running = true;
bool iconAdded = false, watchQQ = false, qqPresent = true, manualReveal = false;
ULONGLONG nextPulse = 0;
ULONGLONG nextWatch = 0;
LSTATUS startupError = ERROR_SUCCESS;
std::unique_ptr<tank::StartupStore> startup;
std::filesystem::path logDirectory;
UINT taskbarCreated = 0;
std::wstring lastPulse = L"尚未发送";

std::wstring Now() {
    SYSTEMTIME t{}; GetLocalTime(&t);
    wchar_t text[64]{};
    swprintf_s(text, L"%04u-%02u-%02u %02u:%02u:%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return text;
}
std::string Utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}
unsigned long long Number(HWND handle) { return reinterpret_cast<UINT_PTR>(handle); }
std::wstring OwnPath() {
    wchar_t path[32768]{}; GetModuleFileNameW(nullptr, path, 32768); return path;
}
bool QQRunning() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return true;
    PROCESSENTRY32W item{}; item.dwSize = sizeof(item);
    bool found = false;
    if (Process32FirstW(snapshot, &item)) do {
        if (_wcsicmp(item.szExeFile, L"QQGame.exe") == 0 || _wcsicmp(item.szExeFile, L"QQGameX64.exe") == 0 ||
            _wcsicmp(item.szExeFile, L"QQMicroGameBox.exe") == 0) { found = true; break; }
    } while (Process32NextW(snapshot, &item));
    CloseHandle(snapshot); return found;
}
void InitLogs() {
    wchar_t appData[32768]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", appData, 32768) == 0) return;
    logDirectory = toolbox::DataDir(L"logs/keepalive");
    std::error_code error; std::filesystem::create_directories(logDirectory, error);
    if (error) logDirectory.clear();
}
void Log(const std::string& event, const tank::Candidate* target = nullptr, DWORD error = 0) {
    if (logDirectory.empty()) return;
    const auto path = logDirectory / L"events.jsonl";
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (!ec && size > 2 * 1024 * 1024) {
        MoveFileExW(path.c_str(), (logDirectory / L"events.previous.jsonl").c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    std::ofstream out(path, std::ios::app);
    out << "{\"time\":\"" << Utf8(Now()) << "\",\"event\":\"" << event << "\",\"error\":" << error;
    if (target) out << ",\"browser\":" << Number(target->browser) << ",\"input\":" << Number(target->input)
        << ",\"pid\":" << target->pid << ",\"visible\":" << (target->visible ? "true" : "false");
    out << "}\n";
}
void WriteStatus() {
    if (logDirectory.empty()) return;
    const auto temp = logDirectory / L"status.tmp";
    std::ofstream out(temp);
    out << "{\"time\":\"" << Utf8(Now()) << "\",\"running\":" << (running ? "true" : "false")
        << ",\"paused\":" << (paused ? "true" : "false")
        << ",\"online_verified\":false,\"interval_seconds\":30,\"tray_only\":true"
        << ",\"watch_qq\":" << (watchQQ ? "true" : "false") << ",\"targets\":[";
    bool first = true;
    for (const auto& target : targets) {
        if (!first) out << ',';
        first = false;
        const auto& count = counts[target.browser];
        out << "{\"browser\":" << Number(target.browser) << ",\"input\":" << Number(target.input)
            << ",\"pid\":" << target.pid << ",\"visible\":" << (target.visible ? "true" : "false")
            << ",\"queued\":" << count.queued << ",\"failed\":" << count.failed << '}';
    }
    out << "]}\n"; out.close();
    if (out) MoveFileExW(temp.c_str(), (logDirectory / L"status.json").c_str(), MOVEFILE_REPLACE_EXISTING);
}
void RemoveTray() {
    if (iconAdded) Shell_NotifyIconW(NIM_DELETE, &tray);
    iconAdded = false;
}
void RefreshDisplay() {
    if (watchQQ && !qqPresent && !manualReveal) { RemoveTray(); return; }
    tray.cbSize = sizeof(tray); tray.hWnd = window; tray.uID = 1;
    tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP; tray.uCallbackMessage = kTrayMessage;
    tray.hIcon = paused || targets.empty() ? pausedIcon : activeIcon;
    std::wstring tip = paused ? L"坦克风暴保活：已暂停" : L"坦克风暴保活：" + std::to_wstring(targets.size()) + L" 个页面";
    wcsncpy_s(tray.szTip, tip.c_str(), _TRUNCATE);
    if (!iconAdded) iconAdded = Shell_NotifyIconW(NIM_ADD, &tray) != FALSE;
    else Shell_NotifyIconW(NIM_MODIFY, &tray);
}
void Cycle(bool send) {
    targets = tank::DiscoverTargets();
    std::map<HWND, Counts> active;
    for (const auto& target : targets) active[target.browser] = counts[target.browser];
    counts.swap(active);
    if (send && !paused) {
        for (const auto& target : targets) {
            const auto delivery = tank::PulseTarget(target, alternate);
            auto& count = counts[target.browser];
            if (delivery.queued) ++count.queued; else ++count.failed;
            Log(delivery.queued ? "move_queued" : "move_rejected", &target, delivery.error);
        }
        alternate = !alternate;
        if (!targets.empty()) lastPulse = Now();
    }
    WriteStatus(); RefreshDisplay();
}
void TogglePause() {
    paused = !paused; Log(paused ? "paused" : "resumed");
    nextPulse = GetTickCount64() + kIntervalMs;
    Cycle(!paused);
}
void StartupChoice(tank::StartupMode mode) {
    startupError = startup->Set(mode);
    Log(startupError == ERROR_SUCCESS ? "startup_changed" : "startup_change_failed", nullptr, static_cast<DWORD>(startupError));
}
void ShowMenu() {
    HMENU menu = CreatePopupMenu();
    const std::wstring summary = (paused ? L"已暂停 · " : L"每 30 秒保活 · ") + std::to_wstring(targets.size()) + L" 个游戏页面";
    AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, summary.c_str());
    unsigned int index = 0;
    for (const auto& target : targets) {
        if (index == 20) { AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, L"其他页面也在处理，详见本地日志"); break; }
        const auto& count = counts[target.browser];
        const std::wstring line = L"页面 " + std::to_wstring(++index) + (target.visible ? L"（可见）" : L"（隐藏）") +
            L"  发送 " + std::to_wstring(count.queued) + L" / 失败 " + std::to_wstring(count.failed);
        AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, line.c_str());
    }
    AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, (L"最近发送：" + lastPulse).c_str());
    AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, L"在线效果请在游戏内确认");
    if (startupError) AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, (L"启动设置失败：" + std::to_wstring(startupError)).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kPause, paused ? L"继续保活" : L"暂停保活");
    HMENU modes = CreatePopupMenu(); const auto mode = startup->Read();
    AppendMenuW(modes, MF_STRING | (mode == tank::StartupMode::Disabled ? MF_CHECKED : 0), kStartupNone, L"不自动启动");
    AppendMenuW(modes, MF_STRING | (mode == tank::StartupMode::Windows ? MF_CHECKED : 0), kStartupWindows, L"登录 Windows 后启动");
    AppendMenuW(modes, MF_STRING | (mode == tank::StartupMode::FollowQQ ? MF_CHECKED : 0), kStartupQQ, L"跟随 QQ 游戏（后台等待）");
    if (mode == tank::StartupMode::Unrecognized) AppendMenuW(modes, MF_STRING | MF_DISABLED, 0, L"启动项位于其他位置或无法读取");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(modes), L"自动启动");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr); AppendMenuW(menu, MF_STRING, kExit, L"退出助手");
    POINT point{}; GetCursorPos(&point); HWND previous = GetForegroundWindow();
    // Explicit tray clicks alone may own menu focus. Timer/startup/pulse paths
    // never activate windows. Restore focus unless the user changed it.
    SetForegroundWindow(window);
    const UINT command = static_cast<UINT>(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
        point.x, point.y, 0, window, nullptr));
    PostMessageW(window, WM_NULL, 0, 0);
    if (GetForegroundWindow() == window && IsWindow(previous)) SetForegroundWindow(previous);
    DestroyMenu(menu);
    if (command) SendMessageW(window, WM_COMMAND, command, 0);
}
LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    if (taskbarCreated && message == taskbarCreated) { iconAdded = false; RefreshDisplay(); return 0; }
    switch (message) {
    case WM_TIMER:
        if (w == kTimer) {
            const auto now = GetTickCount64();
            if (watchQQ && now >= nextWatch) {
                nextWatch = now + 5000; qqPresent = QQRunning();
                if (qqPresent) manualReveal = false;
                RefreshDisplay();
            }
            const bool due = now >= nextPulse;
            if (due) nextPulse = now + kIntervalMs;
            if (due) Cycle(!toolbox::testMode && (!watchQQ || qqPresent));
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case kShow: manualReveal = true; RefreshDisplay(); break;
        case kPause: TogglePause(); break;
        case kForcePause: if (!paused) TogglePause(); break;
        case kForceResume: if (paused) TogglePause(); break;
        case kHide: manualReveal = false; RefreshDisplay(); break;
        case kExit: DestroyWindow(hwnd); break;
        case kStartupNone: StartupChoice(tank::StartupMode::Disabled); break;
        case kStartupWindows: StartupChoice(tank::StartupMode::Windows); break;
        case kStartupQQ: StartupChoice(tank::StartupMode::FollowQQ); break;
        }
        return 0;
    case kTrayMessage:
        if (l == WM_LBUTTONUP || l == WM_RBUTTONUP || l == WM_CONTEXTMENU) ShowMenu();
        return 0;
    case WM_CLOSE: return 0;
    case WM_DESTROY:
        KillTimer(hwnd, kTimer); RemoveTray();
        paused = true; running = false; WriteStatus(); Log("stopped"); PostQuitMessage(0); return 0;
    default: return DefWindowProcW(hwnd, message, w, l);
    }
}
int Diagnostic(const std::filesystem::path& path, bool pulse) {
    const auto found = tank::DiscoverTargets();
    std::ofstream out(path);
    if (!out) return 2;
    POINT before{}, after{}; GetCursorPos(&before); HWND foreground = GetForegroundWindow();
    out << "{\"online_verified\":false,\"pulse_requested\":" << (pulse ? "true" : "false") << ",\"targets\":[";
    bool first = true; unsigned int queued = 0;
    for (const auto& target : found) {
        if (!first) out << ',';
        first = false;
        const auto result = pulse ? tank::PulseTarget(target, false) : tank::Delivery{};
        if (result.queued) ++queued;
        out << "{\"root\":" << Number(target.root) << ",\"browser\":" << Number(target.browser)
            << ",\"input\":" << Number(target.input) << ",\"pid\":" << target.pid
            << ",\"visible\":" << (target.visible ? "true" : "false") << ",\"width\":" << target.width
            << ",\"height\":" << target.height << ",\"queued\":" << (result.queued ? "true" : "false")
            << ",\"error\":" << result.error << '}';
    }
    if (pulse) Sleep(100);
    GetCursorPos(&after);
    out << "],\"cursor_unchanged\":" << ((before.x == after.x && before.y == after.y) ? "true" : "false")
        << ",\"foreground_unchanged\":" << (foreground == GetForegroundWindow() ? "true" : "false") << "}\n";
    out.close();
    if (!out) return 2;
    return found.empty() || (pulse && queued != found.size()) ? 1 : 0;
}
int ControlExisting(UINT command) {
    HWND existing = FindWindowW(kWindowClass, kTitle);
    if (!existing) return 1;
    DWORD pid = 0; GetWindowThreadProcessId(existing, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return 1;
    wchar_t ownerPath[32768]{}, ownPath[32768]{}; DWORD size = 32768;
    const bool valid = QueryFullProcessImageNameW(process, 0, ownerPath, &size) != FALSE;
    CloseHandle(process); GetModuleFileNameW(nullptr, ownPath, 32768);
    if (!valid || _wcsicmp(ownerPath, ownPath) != 0) return 1;
    return PostMessageW(existing, WM_COMMAND, command, 0) ? 0 : 1;
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    startup = std::make_unique<tank::StartupStore>(OwnPath());
    HANDLE mutex = CreateMutexW(nullptr, TRUE, toolbox::testMode ? L"Local\\CppToolbox.KeepAlive.Test" : L"Local\\CppToolbox.KeepAlive.1");
    if (!mutex) return 2;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex); return watchQQ ? 0 : ControlExisting(kShow);
    }
    InitLogs(); Log("started");
    WNDCLASSW cls{}; cls.lpfnWndProc = WindowProc; cls.hInstance = instance;
    cls.lpszClassName = kWindowClass;
    activeIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(201), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    pausedIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(202), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    if (!activeIcon || !pausedIcon) { CloseHandle(mutex); return 2; }
    cls.hIcon = activeIcon;
    if (!RegisterClassW(&cls)) { CloseHandle(mutex); return 2; }
    window = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, kTitle,
        WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!window) { CloseHandle(mutex); return 2; }
    qqPresent = !watchQQ || QQRunning();
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    nextPulse = GetTickCount64() + kIntervalMs; Cycle(!toolbox::testMode);
    if (!SetTimer(window, kTimer, 1000, nullptr)) {
        DestroyWindow(window); DestroyIcon(activeIcon); DestroyIcon(pausedIcon); CloseHandle(mutex); return 2;
    }
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    DestroyIcon(activeIcon); DestroyIcon(pausedIcon); CloseHandle(mutex); return 0;
}
