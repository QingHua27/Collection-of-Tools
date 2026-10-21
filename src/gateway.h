#pragma once
#include <windows.h>
#include <shellapi.h>
#include <atomic>
#include <string>
#include <thread>
#include <array>
#include <functional>

namespace toolbox {
enum Module : int { Screenshot, Recording, Countdown, KeepAlive, League, AceOptimizer, Count };
enum Action : int { Refresh, Primary, Secondary, Configure, Stop, TestCountdown, TestRecording, TestRecordingStop, IoOption, MemoryOption, HotkeyFirst=100 };
extern std::atomic<int> screenshotPreset;
const wchar_t* ScreenshotPresetName(int index);
constexpr UINT WM_HOST_CALL = WM_APP + 0x615;
extern HWND host;
inline void OnHost(const std::function<void()>& fn) {
    if(GetCurrentThreadId()==GetWindowThreadProcessId(host,nullptr)) fn();
    else SendMessageW(host,WM_HOST_CALL,0,reinterpret_cast<LPARAM>(&fn));
}
constexpr UINT WM_ACTION = WM_APP + 0x610;
constexpr UINT WM_STATE = WM_APP + 0x611;
constexpr UINT WM_READY = WM_APP + 0x612;
constexpr UINT WM_BALLOON = WM_APP + 0x613;
constexpr UINT WM_REVEAL = WM_APP + 0x614;
struct State { Module module; std::wstring text; };
struct Notice { std::wstring title, text; DWORD flags; };
struct Slot {
    std::atomic<HWND> window{nullptr};
    std::atomic<DWORD> threadId{0};
    std::atomic<bool> running{false};
    std::atomic<bool> stopRequested{false};
    std::thread thread;
};
extern HWND host;
extern std::array<HWND, Count> pages;
extern std::array<Slot, Count> slots;
extern std::atomic<bool> shuttingDown;
extern bool testMode;
extern thread_local Module currentModule;
std::wstring DataDir(const wchar_t* child = L"");
void Log(const std::wstring& message);
void StateText(Module id, const std::wstring& text);
void Publish(Module id, HWND window, bool dock = false);
BOOL NextMessage(LPMSG msg, HWND window, UINT low, UINT high, void(*action)(Action));
BOOL NotifyIcon(DWORD operation, PNOTIFYICONDATAW icon);
void Start(Module id);
void Send(Module id, Action action);
bool AllStopped();
void RunScreenshot();
void RunRecording();
void RunCountdown();
bool CountdownDialogMessage(MSG* message);
void RunKeepAlive();
void RunLeague();
void RunAce();
}
