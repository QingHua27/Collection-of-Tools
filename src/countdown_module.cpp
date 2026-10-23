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
            RECT client{},format{};GetClientRect(edit,&client);SendMessageW(edit,EM_GETRECT,0,(LPARAM)&format);
            if(format.top<2 || std::abs(format.top-(client.bottom-format.bottom))>2)
                throw std::runtime_error("countdown input text must be vertically centered");
            SetFocus(edit); SetWindowTextW(edit,L""); SendMessageW(edit,WM_CHAR,L'8',0);
            if(getEditInt(edit,0)!=8) throw std::runtime_error("countdown keyboard input failed");
        }
        for(HWND edit : {g_edDay,g_edHour,g_edMin,g_edSec}) {
            int limit=timeFieldMaximum(edit);SetWindowTextW(edit,std::to_wstring(limit).c_str());SendMessageW(edit,EM_SETSEL,-1,-1);SendMessageW(edit,WM_CHAR,L'9',0);
            if(getEditInt(edit,0)!=limit)throw std::runtime_error("time field accepted excessive keyboard input");
            SetWindowTextW(edit,L"255255255");if(getEditInt(edit,0)!=limit)throw std::runtime_error("time field range normalization failed");
            SetWindowTextW(edit,L"");SendMessageW(edit,WM_KILLFOCUS,0,0);if(GetWindowTextLengthW(edit)!=1)throw std::runtime_error("empty time field must normalize to zero");
            if(acceptsTimeInput(edit,L"-1") || acceptsTimeInput(edit,L"1x"))throw std::runtime_error("invalid pasted numeric text accepted");
        }
        for(HWND edit : {g_edDay,g_edHour,g_edMin}) {
            MSG tab{};tab.hwnd=edit;tab.message=WM_KEYDOWN;tab.wParam=VK_TAB;
            auto code=SendMessageW(edit,WM_GETDLGCODE,VK_TAB,(LPARAM)&tab);
            if(code&(DLGC_WANTTAB|DLGC_WANTALLKEYS))throw std::runtime_error("time input consumes Tab navigation");
            SetFocus(edit);HWND expected=GetNextDlgTabItem(g_hwnd,edit,FALSE);
            if(!IsDialogMessageW(g_hwnd,&tab) || GetFocus()!=expected)throw std::runtime_error("time input Tab focus navigation failed");
        }
        BYTE savedKeys[256]{},shiftKeys[256]{};GetKeyboardState(savedKeys);
        memcpy(shiftKeys,savedKeys,sizeof(savedKeys));shiftKeys[VK_SHIFT]=0x80;
        for(HWND edit : {g_edSec,g_edMin,g_edHour}) {
            MSG tab{};tab.hwnd=edit;tab.message=WM_KEYDOWN;tab.wParam=VK_TAB;
            SetFocus(edit);HWND expected=GetNextDlgTabItem(g_hwnd,edit,TRUE);
            SetKeyboardState(shiftKeys);bool handled=IsDialogMessageW(g_hwnd,&tab)!=FALSE;HWND focused=GetFocus();SetKeyboardState(savedKeys);
            if(!handled || focused!=expected)throw std::runtime_error("time input Shift+Tab navigation failed");
        }
        resetCountdown();
        auto sessionPath=exeDir()+L"\\session.dat";
        auto recover=[&](timer_session::State saved) {
            if(!timer_session::Save(sessionPath,saved))throw std::runtime_error("test session write failed");
            restoreSession();
        };
        timer_session::State saved;saved.mode=1;saved.total=10000;saved.remaining=10000;saved.deadline=timer_session::Now()+5000;saved.popup=0;saved.flash=0;
        recover(saved);
        if(g_mode!=MODE_RUN || g_elapsedMs<4900 || g_elapsedMs>6000)throw std::runtime_error("running countdown recovery failed");
        saved.mode=2;saved.remaining=2345;recover(saved);
        if(g_mode!=MODE_PAUSE || g_totalMs-g_elapsedMs!=2345)throw std::runtime_error("paused countdown recovery failed");
        togglePause();
        if(g_mode!=MODE_RUN || std::abs(g_deadlineMs-timer_session::Now()-2345)>200)throw std::runtime_error("recovered pause resume failed");
        saved.mode=1;saved.alarm=1;saved.deadline=timer_session::Now()+5000;recover(saved);
        if(g_mode!=MODE_RUN || !g_alarmMode || IsWindowEnabled(g_btnPause))throw std::runtime_error("alarm recovery failed");
        for(unsigned alarm : {0u,1u}) {
            saved.alarm=alarm;saved.deadline=timer_session::Now()-1000;unsigned completed=g_completedCount;recover(saved);
            if(g_mode!=MODE_IDLE || g_completedCount!=completed+1)throw std::runtime_error("overdue recovery must remind once");
            timer_session::State consumed;
            if(timer_session::Load(sessionPath,consumed))throw std::runtime_error("delivered reminder must be consumed");
            restoreSession();if(g_completedCount!=completed+1)throw std::runtime_error("reminder replayed on next recovery");
        }
        SendMessageW(g_modeChoice,BM_CLICK,0,0);
        resetCountdown(); g_popup = false; g_flash = false; g_soundFile.clear();
        SetWindowTextW(g_edDay, L"0"); SetWindowTextW(g_edHour, L"0");
        SetWindowTextW(g_edMin, L"0"); SetWindowTextW(g_edSec, L"2");
        SetFocus(g_edSec);MSG enter{};enter.hwnd=g_edSec;enter.message=WM_KEYDOWN;enter.wParam=VK_RETURN;enter.lParam=1;
        if(!IsDialogMessageW(g_hwnd,&enter))DispatchMessageW(&enter);
        if(g_mode!=MODE_RUN || g_totalMs!=2000)throw std::runtime_error("Enter from time input must start countdown");
        auto started=g_lastTick;SendMessageW(g_edSec,WM_KEYDOWN,VK_RETURN,1LL<<30);
        if(g_lastTick!=started)throw std::runtime_error("held Enter must not restart countdown");
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
    if (!published) { published = true; toolbox::Publish(toolbox::Countdown, g_hwnd, true); CountdownAction(toolbox::Refresh); if(!toolbox::testMode)PostMessageW(g_hwnd,WM_RESTORE_SESSION,0,0); }
    return toolbox::NextMessage(msg, h, low, high, CountdownAction);
}
void toolbox::RunCountdown() { countdownWorker=GetCurrentThreadId(); CountdownEntry(GetModuleHandleW(nullptr), nullptr, nullptr, SW_HIDE); }

bool toolbox::CountdownDialogMessage(MSG* message) {
    if(!IsWindow(g_hwnd) || !IsWindowVisible(g_hwnd)) return false;
    if(message->hwnd!=g_hwnd && !IsChild(g_hwnd,message->hwnd)) return false;
    return IsDialogMessageW(g_hwnd,message)!=FALSE;
}
