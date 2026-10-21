#include "gateway.h"
#include <filesystem>
#include <mutex>
#include <fstream>
#include <stdexcept>

namespace toolbox {
HWND host = nullptr;
std::array<HWND, Count> pages{};
std::array<Slot, Count> slots;
std::atomic<bool> shuttingDown{false};
bool testMode = false;
thread_local Module currentModule = Screenshot;

std::wstring DataDir(const wchar_t* child) {
    wchar_t local[32768]{};
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768))
        throw std::runtime_error("LOCALAPPDATA unavailable");
    auto base = std::filesystem::path(local) / L"CppToolbox";
    if (testMode) base /= L"self-test";
    auto dir = base / child;
    std::filesystem::create_directories(dir);
    return dir.wstring();
}
void Log(const std::wstring& message) {
    static std::mutex lock;
    std::lock_guard<std::mutex> guard(lock);
    try {
        auto path = std::filesystem::path(DataDir(L"logs")) / L"toolbox.log";
        std::error_code ec;
        if (std::filesystem::file_size(path, ec) > 2 * 1024 * 1024 && !ec)
            std::filesystem::rename(path, path.wstring() + L".previous", ec);
        std::ofstream out(path, std::ios::app);
        SYSTEMTIME t{}; GetLocalTime(&t);
        out << t.wYear << '-' << t.wMonth << '-' << t.wDay << ' ' << t.wHour << ':' << t.wMinute << ':' << t.wSecond << ' ';
        int n = WideCharToMultiByte(CP_UTF8, 0, message.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string utf8(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, message.c_str(), -1, utf8.data(), n, nullptr, nullptr);
        if (n > 0) utf8.pop_back();
        out << utf8 << '\n';
    } catch (...) { OutputDebugStringW(message.c_str()); }
}
void StateText(Module id, const std::wstring& text) {
    auto state = new State{id, text};
    if (!PostMessageW(host, WM_STATE, 0, reinterpret_cast<LPARAM>(state))) delete state;
}
void Publish(Module id, HWND window, bool dock) {
    auto& slot = slots[id];
    slot.window = window;
    if (dock && window) {
        ShowWindow(window, SW_HIDE);
        SetParent(window, pages[id]);
        LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
        style &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
        SetWindowLongPtrW(window, GWL_STYLE, style | WS_CHILD | WS_CLIPCHILDREN);
        SetWindowLongPtrW(window, GWL_EXSTYLE, GetWindowLongPtrW(window, GWL_EXSTYLE) & ~WS_EX_APPWINDOW);
        RECT client{}; GetClientRect(pages[id], &client);
        int x=0, y=0, width=client.right, height=client.bottom;
        if (id==Countdown) {
            UINT dpi=GetDpiForWindow(pages[id]);
            width=MulDiv(520,dpi,96); height=MulDiv(444,dpi,96);
            x=(client.right-width)/2; y=MulDiv(24,dpi,96);
        }
        SetWindowPos(window, nullptr, x, y, width, height,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
    Log(L"module ready " + std::to_wstring(id));
    PostMessageW(host, WM_READY, id, reinterpret_cast<LPARAM>(window));
}
BOOL NextMessage(LPMSG msg, HWND window, UINT low, UINT high, void(*action)(Action)) {
    for (;;) {
        if (slots[currentModule].stopRequested.exchange(false)) action(Stop);
        const BOOL got = GetMessageW(msg, window, low, high);
        if (got <= 0) return FALSE;
        if (!msg->hwnd && msg->message == WM_ACTION) {
            action(static_cast<Action>(msg->wParam));
            continue;
        }
        return TRUE;
    }
}
BOOL NotifyIcon(DWORD operation, PNOTIFYICONDATAW icon) {
    // Modules never create their own tray icon. Forward notifications to the host.
    if (operation == NIM_MODIFY && icon && (icon->uFlags & NIF_INFO) && icon->szInfo[0]) {
        auto notice = new Notice{icon->szInfoTitle, icon->szInfo, icon->dwInfoFlags};
        if (!PostMessageW(host, WM_BALLOON, 0, reinterpret_cast<LPARAM>(notice))) delete notice;
    }
    return TRUE;
}
void Start(Module id) {
    auto& slot = slots[id];
    if (slot.running.exchange(true)) return;
    if (slot.thread.joinable()) slot.thread.join();
    slot.stopRequested = false;
    slot.thread = std::thread([id] {
        currentModule = id;
        MSG msg{}; PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
        slots[id].threadId = GetCurrentThreadId();
        try {
            switch (id) {
            case Screenshot: RunScreenshot(); break;
            case Recording: RunRecording(); break;
            case Countdown: RunCountdown(); break;
            case KeepAlive: RunKeepAlive(); break;
            case League: RunLeague(); break;
            case AceOptimizer: RunAce(); break;
            default: break;
            }
        } catch (const std::exception& e) {
            Log(L"module exception " + std::to_wstring(id));
            StateText(id, L"模块启动失败，请查看日志。");
            OutputDebugStringA(e.what());
        } catch (...) { Log(L"module exception " + std::to_wstring(id)); }
        slots[id].window = nullptr;
        slots[id].threadId = 0;
        slots[id].running = false;
        StateText(id, L"已停止");
        Log(L"module stopped " + std::to_wstring(id));
    });
}
void Send(Module id, Action action) {
    auto& slot = slots[id];
    DWORD tid = slot.threadId.load();
    if (action == Stop) {
        slot.stopRequested = true;
        // Native dialog loops can consume thread messages. Close modal windows so
        // their caller returns to NextMessage and observes the persistent request.
        if(tid) EnumThreadWindows(tid, [](HWND h,LPARAM root)->BOOL {
            if(h!=reinterpret_cast<HWND>(root)) {
                wchar_t cls[64]{}; GetClassNameW(h,cls,64);
                if(wcscmp(cls,L"#32770")==0 || wcscmp(cls,L"JianJieSettings")==0 ||
                   wcscmp(cls,L"LiteCapHotkeyWnd")==0)
                    PostMessageW(h,WM_CLOSE,0,0);
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(slot.window.load()));
    }
    if (tid) PostThreadMessageW(tid, WM_ACTION, action, 0);
}
bool AllStopped() {
    for (auto& slot : slots) if (slot.running) return false;
    return true;
}
}
