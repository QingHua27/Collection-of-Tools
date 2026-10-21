#include <stdexcept>
#include "gateway.h"
static DWORD countdownWorker=0;
static HWND CountdownCreate(DWORD,LPCWSTR,LPCWSTR,DWORD,int,int,int,int,HWND,HMENU,HINSTANCE,LPVOID);
static void CountdownQuit(int code) { PostThreadMessageW(countdownWorker,WM_QUIT,code,0); }
#define CreateWindowExW CountdownCreate
#define PostQuitMessage CountdownQuit
static BOOL CountdownMessage(LPMSG, HWND, UINT, UINT);
#define wWinMain CountdownEntry
#define GetMessageW CountdownMessage
#define Shell_NotifyIconW toolbox::NotifyIcon
#include "modules/timer/main.cpp"
#undef Shell_NotifyIconW
#undef GetMessageW
#undef wWinMain
#undef CreateWindowExW
#undef PostQuitMessage
static HWND CountdownCreate(DWORD ex,LPCWSTR cls,LPCWSTR name,DWORD style,int x,int y,int w,int h,HWND parent,HMENU menu,HINSTANCE instance,LPVOID data) {
    HWND result=nullptr;
    auto create=[&] { result=CreateWindowExW(ex,cls,name,style,x,y,w,h,parent,menu,instance,data); };
    if(cls && !IS_INTRESOURCE(cls) && wcscmp(cls,L"CountdownGuiClass")==0) toolbox::OnHost(create);
    else create();
    return result;
}

static void CountdownActionImpl(toolbox::Action action) {
    using namespace toolbox;
    if (action == Primary) { if (g_mode == MODE_IDLE) startCountdown(); else togglePause(); }
    if (action == Secondary) resetCountdown();
    if (action == TestCountdown) {
        if(GetWindowThreadProcessId(g_hwnd,nullptr)!=GetWindowThreadProcessId(host,nullptr))
            throw std::runtime_error("countdown controls must share host input thread");
        resetCountdown();
        SendMessageW(g_alarmChoice,BM_CLICK,0,0);
        if(!g_alarmMode || IsWindowEnabled(g_edDay) || SendMessageW(g_alarmChoice,BM_GETCHECK,0,0)!=BST_CHECKED)
            throw std::runtime_error("alarm mode radio click failed");
        for(HWND edit : {g_edHour,g_edMin,g_edSec}) {
            if(!IsWindowEnabled(edit)) throw std::runtime_error("alarm edit disabled");
            SetFocus(edit); SetWindowTextW(edit,L""); SendMessageW(edit,WM_CHAR,L'7',0);
            if(getEditInt(edit,0)!=7) throw std::runtime_error("alarm keyboard input failed");
        }
        SendMessageW(g_modeChoice,BM_CLICK,0,0);
        if(g_alarmMode || !IsWindowEnabled(g_edDay) || !IsWindowEnabled(g_btnStart))
            throw std::runtime_error("countdown mode radio click failed");
        for(HWND edit : {g_edDay,g_edHour,g_edMin,g_edSec}) {
            if(!IsWindowEnabled(edit)) throw std::runtime_error("countdown edit disabled");
            SetFocus(edit); SetWindowTextW(edit,L""); SendMessageW(edit,WM_CHAR,L'8',0);
            if(getEditInt(edit,0)!=8) throw std::runtime_error("countdown keyboard input failed");
        }
        resetCountdown(); g_popup = false; g_flash = false; g_soundFile.clear();
        SetWindowTextW(g_edDay, L"0"); SetWindowTextW(g_edHour, L"0");
        SetWindowTextW(g_edMin, L"0"); SetWindowTextW(g_edSec, L"2"); startCountdown();
    }
    if (action == Refresh) {
        const wchar_t* text = g_mode == MODE_RUN ? L"倒计时进行中" : g_mode == MODE_PAUSE ? L"倒计时已暂停" : g_mode == MODE_DONE ? L"倒计时已完成" : L"倒计时就绪";
        StateText(Countdown, g_mode == MODE_IDLE && g_completedCount ? L"上次倒计时已完成，可重新开始。" : text);
    }
    if (action == Stop && IsWindow(g_hwnd)) { g_quitting = true; DestroyWindow(g_hwnd); }
}
static void CountdownAction(toolbox::Action action) {
    toolbox::OnHost([&] { CountdownActionImpl(action); });
}
static BOOL CountdownMessage(LPMSG msg, HWND h, UINT low, UINT high) {
    static bool published = false;
    if (!published) { published = true; toolbox::Publish(toolbox::Countdown, g_hwnd, true); CountdownAction(toolbox::Refresh); }
    return toolbox::NextMessage(msg, h, low, high, CountdownAction);
}
void toolbox::RunCountdown() { countdownWorker=GetCurrentThreadId(); CountdownEntry(GetModuleHandleW(nullptr), nullptr, nullptr, SW_HIDE); }

bool toolbox::CountdownDialogMessage(MSG* message) {
    if(!IsWindow(g_hwnd) || !IsWindowVisible(g_hwnd)) return false;
    if(message->hwnd!=g_hwnd && !IsChild(g_hwnd,message->hwnd)) return false;
    return IsDialogMessageW(g_hwnd,message)!=FALSE;
}
