#include "gateway.h"
#include "ace.h"
#include "preview.h"
#include <commctrl.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

using namespace toolbox;
namespace {
constexpr wchar_t ClassName[] = L"CppToolbox.Main.1";
const wchar_t* MainClass() { return testMode?L"CppToolbox.Main.Test.1":ClassName; }
constexpr UINT TrayMessage = WM_APP + 1;
constexpr int NavBase = 100;
const wchar_t* Names[] = {L"截图", L"录屏", L"倒计时", L"后台保活", L"LOL 助手", L"ACE 优化"};
const wchar_t* Descriptions[] = {
    L"框选屏幕，标注、识别文字或翻译。",
    L"录制桌面、指定区域或窗口，保留声音与录制记录。",
    L"选择倒计时或定时闹钟，到点播放提示音并提醒你。",
    L"管理坦克风暴页面的后台保活。",
    L"自动接受对战确认，导出游戏内实时攻速。",
    L"管理 ACE 的 CPU、磁盘 I/O 和内存页优先级，减少与游戏的资源争用。"
};
HWND navigation[6]{}, content[6]{}, title{}, subtitle{}, footer{};
HWND enableKeep{}, enableLeague{}, aceLaunch{}, startupButton{};
HWND closeButton{};
HFONT font{}, titleFont{}, smallFont{};
HBRUSH background{}, white{};
NOTIFYICONDATAW tray{};
UINT taskbarCreated = 0;
UINT dpi = 96;
int selected = 0;
bool closing = false;
bool keepEnabled = false, leagueEnabled = false;
bool moduleStopping[Count]{};
bool restartElevated=false;
bool closeToTray=true;
DWORD testStart = 0;
int testPhase = 0;
int testFailures = 0;
bool previews = false;
std::filesystem::file_time_type testWallStart;
std::vector<std::string> testResults;
std::array<std::wstring, Count> status;
void Command(int id);
std::wstring Ini() { return DataDir() + L"\\toolbox.ini"; }
void SetCloseBehavior(bool toTray) {
    closeToTray=toTray;
    WritePrivateProfileStringW(L"general",L"close_action",toTray?L"tray":L"exit",Ini().c_str());
    SetWindowTextW(closeButton,toTray?L"关闭：托盘":L"关闭：退出");
}
void CloseBehaviorMenu() {
    HMENU menu=CreatePopupMenu();
    AppendMenuW(menu,MF_STRING|(closeToTray?MF_CHECKED:0),1210,L"最小化到托盘（默认）");
    AppendMenuW(menu,MF_STRING|(!closeToTray?MF_CHECKED:0),1211,L"直接退出工具箱");
    POINT p{}; GetCursorPos(&p);
    UINT cmd=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,host,nullptr);
    DestroyMenu(menu);
    if(cmd) SetCloseBehavior(cmd==1210);
}
constexpr int UiPercent=85;
int Scale(int value) { return MulDiv(value,dpi*UiPercent,9600); }
HMENU hotkeyMenu=nullptr;
bool hotkeyMenuOpened=false;
void ScreenshotHotkeyMenu() {
    hotkeyMenu=CreatePopupMenu(); hotkeyMenuOpened=false;
    for(int i=0;i<6;++i) AppendMenuW(hotkeyMenu,MF_STRING|(screenshotPreset==i?MF_CHECKED:0),1300+i,ScreenshotPresetName(i));
    POINT point{}; GetCursorPos(&point); SetForegroundWindow(host);
    if(testMode) SetTimer(host,7,180,[](HWND hwnd,UINT,UINT_PTR timer,DWORD){KillTimer(hwnd,timer); EndMenu();});
    UINT chosen=TrackPopupMenu(hotkeyMenu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,host,nullptr);
    DestroyMenu(hotkeyMenu); hotkeyMenu=nullptr;
    if(chosen>=1300 && chosen<1306) Send(Screenshot,static_cast<Action>(HotkeyFirst+chosen-1300));
}
void SaveFlag(const wchar_t* name, bool on) {
    WritePrivateProfileStringW(L"modules", name, on ? L"1" : L"0", Ini().c_str());
}
HWND Control(HWND parent, const wchar_t* cls, const wchar_t* text, int id, DWORD style = 0) {
    HWND h = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
        0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return h;
}
void Position(HWND h, int x, int y, int width, int height) {
    MoveWindow(h, Scale(x), Scale(y), Scale(width), Scale(height), TRUE);
}
void Reveal(int page) {
    if (page >= 0 && page < 6) selected = page;
    SetWindowTextW(title, Names[selected]); SetWindowTextW(subtitle, Descriptions[selected]);
    for (int i = 0; i < 6; ++i) {
        ShowWindow(content[i], i == selected ? SW_SHOW : SW_HIDE);
        InvalidateRect(navigation[i], nullptr, TRUE);
    }
    SetWindowTextW(footer, selected < Count ? status[selected].c_str() : L"");
    if(selected==Countdown && slots[Countdown].window) ShowWindow(slots[Countdown].window,SW_SHOW);
    if(selected==Recording && slots[Recording].window) Send(Recording,Secondary);
}
void ShowHost(int page = -1) {
    ShowWindow(host, SW_RESTORE); if (page >= 0) Reveal(page);
    SetForegroundWindow(host);
}
bool StartupEnabled() {
    wchar_t value[32768]{}; DWORD bytes = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        L"CppToolbox", RRF_RT_REG_SZ, nullptr, value, &bytes) != ERROR_SUCCESS) return false;
    wchar_t exe[32768]{}; GetModuleFileNameW(nullptr, exe, 32768);
    return std::wstring(value) == L"\"" + std::wstring(exe) + L"\" --tray";
}
void ToggleStartup() {
    bool enable = !StartupEnabled(); HKEY key{};
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (result == ERROR_SUCCESS) {
        if (enable) {
            wchar_t exe[32768]{}; GetModuleFileNameW(nullptr, exe, 32768);
            std::wstring command = L"\"" + std::wstring(exe) + L"\" --tray";
            result = RegSetValueExW(key, L"CppToolbox", 0, REG_SZ,
                reinterpret_cast<const BYTE*>(command.c_str()), static_cast<DWORD>((command.size()+1)*sizeof(wchar_t)));
        } else { result = RegDeleteValueW(key, L"CppToolbox"); if (result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS; }
        RegCloseKey(key);
    }
    if (result != ERROR_SUCCESS) MessageBoxW(host, L"无法更新开机启动项。", L"工具箱", MB_ICONERROR);
    SetWindowTextW(startupButton, StartupEnabled() ? L"自启：开" : L"自启：关");
}
void AddTray() {
    tray.cbSize = sizeof(tray); tray.hWnd = host; tray.uID = 1;
    tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    tray.uCallbackMessage = TrayMessage;
    tray.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
    wcscpy_s(tray.szTip, L"工具箱 · 截图 / 录屏 / 倒计时 / 保活 / LOL / ACE");
    if (!Shell_NotifyIconW(NIM_ADD, &tray)) Log(L"tray icon add failed");
}
void RequestClose() {
    if (closing) return;
    closing = true; shuttingDown = true;
    SetWindowTextW(footer, L"正在保存录制并停止后台任务…");
    for (int i = 0; i < Count; ++i) Send(static_cast<Module>(i), Stop);
    Log(L"shutdown requested");
}
void TrayMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 900, L"打开工具箱");
    AppendMenuW(menu, MF_STRING, 300, L"开始截图");
    for (int i = 1; i < 6; ++i) AppendMenuW(menu, MF_STRING, NavBase+i, Names[i]);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu,MF_STRING,1204,L"设置关闭按钮行为…");
    AppendMenuW(menu, MF_STRING, 1203, L"退出工具箱");
    POINT p{}; GetCursorPos(&p); SetForegroundWindow(host);
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, host, nullptr);
    DestroyMenu(menu); PostMessageW(host, WM_NULL, 0, 0);
    if (cmd) PostMessageW(host, WM_COMMAND, cmd, 0);
}
void Layout() {
    RECT r{}; GetClientRect(host, &r);
    const int w=MulDiv(r.right,9600,dpi*UiPercent), h=MulDiv(r.bottom,9600,dpi*UiPercent);
    for (int i = 0; i < 6; ++i) Position(navigation[i], 6, 58+i*44, 96, 38);
    Position(startupButton, 6, h-164, 96, 30);
    Position(closeButton,6,h-128,96,30);
    Position(GetDlgItem(host,1202), 6, h-92, 96,30);
    Position(GetDlgItem(host,1203), 6, h-56, 96,30);
    Position(title,140,4,w-164,34); Position(subtitle,142,40,w-166,24);
    Position(footer,140,h-40,w-164,26);
    for (int i=0;i<6;++i) Position(content[i],132,64,w-152,h-118);
    Position(GetDlgItem(content[5],2502),24,226,w-200,h-412);
    Position(GetDlgItem(content[5],2503),24,h-170,w-200,44);
    for (int i : {1,2}) {
        HWND embedded = slots[i].window.load(); RECT rc{}; GetClientRect(content[i],&rc);
        int x=0,y=0,width=rc.right,height=rc.bottom;
        if(i==Countdown) { width=MulDiv(520,dpi,96); height=MulDiv(444,dpi,96); x=(rc.right-width)/2; y=Scale(24); }
        if (embedded) SetWindowPos(embedded,nullptr,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE|SWP_ASYNCWINDOWPOS);
    }
}
LRESULT CALLBACK PageProc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    if(message==WM_COMMAND) return SendMessageW(host,message,w,l);
    if(message==WM_CTLCOLORSTATIC) {
        SetBkMode(reinterpret_cast<HDC>(w),TRANSPARENT);
        SetTextColor(reinterpret_cast<HDC>(w),RGB(47,59,76));
        return reinterpret_cast<LRESULT>(white);
    }
    return DefWindowProcW(hwnd,message,w,l);
}
void BuildPages() {
    WNDCLASSW pc{}; pc.hInstance=GetModuleHandleW(nullptr); pc.lpfnWndProc=PageProc;
    pc.lpszClassName=L"CppToolbox.Page"; pc.hbrBackground=white; pc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    RegisterClassW(&pc);
    for (int i=0;i<6;++i) {
        navigation[i] = Control(host,L"BUTTON",Names[i],NavBase+i,BS_OWNERDRAW|WS_TABSTOP);
        content[i] = Control(host,L"CppToolbox.Page",L"",200+i,WS_CLIPCHILDREN);
        if (i<Count) pages[i]=content[i];
    }
    title=Control(host,L"STATIC",L"截图",0); SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(titleFont),TRUE);
    subtitle=Control(host,L"STATIC",Descriptions[0],0); footer=Control(host,L"STATIC",L"正在初始化…",0);
    SendMessageW(footer,WM_SETFONT,reinterpret_cast<WPARAM>(smallFont),TRUE);
    startupButton=Control(host,L"BUTTON",StartupEnabled()?L"自启：开":L"自启：关",1201,BS_PUSHBUTTON|WS_TABSTOP);
    closeButton=Control(host,L"BUTTON",closeToTray?L"关闭：托盘":L"关闭：退出",1204,BS_PUSHBUTTON|WS_TABSTOP);
    Control(host,L"BUTTON",L"配置与日志",1202,BS_PUSHBUTTON|WS_TABSTOP);
    Control(host,L"BUTTON",L"退出工具箱",1203,BS_PUSHBUTTON|WS_TABSTOP);
    auto label=[](int page,const wchar_t* text,int id,int y,int height) {
        HWND h=Control(content[page],L"STATIC",text,id,SS_LEFT); Position(h,24,y,690,height); return h;
    };
    auto button=[](int page,const wchar_t* text,int id,int x,int y,int width) {
        HWND h=Control(content[page],L"BUTTON",text,id,BS_PUSHBUTTON|WS_TABSTOP); Position(h,x,y,width,40); return h;
    };
    label(0,L"让屏幕上的内容，随手可用。",2100,24,40);
    button(0,L"开始截图",300,24,88,190);
    button(0,L"设置截图快捷键",301,230,88,190);
    button(0,L"翻译服务设置",302,436,88,190);
    label(0,L"拖拽框选或单击选择窗口\r\n\r\nR 矩形    E 椭圆    A 箭头    P 画笔    M 马赛克\r\nT 文字    N 序号    O 文字识别    F 翻译\r\n\r\nEnter 复制    Ctrl+S 保存    Ctrl+T 贴图    Esc 取消\r\n\r\n截图和录屏快捷键常驻后台，关闭主窗口会收进托盘。",2101,170,330);
    label(3,L"坦克风暴后台保活",2300,24,32);
    enableKeep=button(3,L"开启后台保活",330,24,84,190);
    button(3,L"暂停 / 继续",331,230,84,160); button(3,L"立即检测并保活",332,406,84,190);
    button(3,L"跟随 QQ 游戏 开 / 关",333,24,140,230);
    label(3,L"未开启。点击上方按钮后开始监视游戏页面。",2301,212,350);
    label(4,L"英雄联盟自动接受与攻速导出",2400,24,32);
    enableLeague=button(4,L"开启 LOL 助手",340,24,84,190);
    button(4,L"自动接受 开 / 关",341,230,84,190); button(4,L"手动接受",342,436,84,150);
    button(4,L"攻速刷新间隔",343,24,140,190); button(4,L"打开攻速输出",344,230,140,190);
    label(4,L"未开启。使用客户端本地 API，攻速输出保留原路径 D:\\LOL.txt。",2401,212,360);
    aceLaunch=button(5,L"启用游戏优先优化",350,24,24,210);
    button(5,L"立即刷新",351,250,24,150);
    HWND admin=button(5,IsElevated()?L"已以管理员运行":L"以管理员权限重启",352,416,24,220);
    EnableWindow(admin,!IsElevated() && !testMode);
    HWND cpu=Control(content[5],L"BUTTON",L"降低 CPU 优先级",353,BS_AUTOCHECKBOX|WS_TABSTOP);
    HWND io=Control(content[5],L"BUTTON",L"降低磁盘 I/O 优先级",354,BS_AUTOCHECKBOX|WS_TABSTOP);
    HWND memory=Control(content[5],L"BUTTON",L"按需让出内存（降低内存页优先级）",355,BS_AUTOCHECKBOX|WS_TABSTOP);
    Position(cpu,24,84,240,30); Position(io,280,84,280,30);
    SendMessageW(cpu,BM_SETCHECK,BST_CHECKED,0); SendMessageW(io,BM_SETCHECK,BST_CHECKED,0);
    Position(memory,24,118,570,30); SendMessageW(memory,BM_SETCHECK,BST_CHECKED,0);
    label(5,L"每 3 秒识别并检查 ACE / SGuard。开启后自动应用；停止或正常退出时恢复原值。\r\n内存紧张时由系统按页优先级回收，不定时强制清空工作集，以免增加回读和卡顿。",2501,164,52);
    Control(content[5],L"EDIT",L"正在检测…",2502,ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_BORDER);
    label(5,L"调度优先级不是 CPU/磁盘/内存硬上限；反作弊驱动 I/O 可能不受影响。\r\nI/O 计数包含缓存等访问；私有提交也不等同实际驻留内存。",2503,560,48);
}
void TestCheck(bool okay,const char* name) {
    testResults.emplace_back(std::string(okay?"PASS ":"FAIL ")+name);
    if (!okay) ++testFailures;
}
void TickTest() {
    if (!testMode || closing) return;
    DWORD elapsed=GetTickCount()-testStart;
    if (testPhase==0) {
        bool ready=true; for(auto& s:slots) ready = ready && s.window.load()!=nullptr;
        if (!ready && elapsed<18000) return;
        TestCheck(ready,"six native source modules initialized");
        RECT compact{}; GetWindowRect(host,&compact);
        TestCheck(compact.right-compact.left<=MulDiv(1020,dpi,96),"compact default window size");
        ScreenshotHotkeyMenu(); TestCheck(hotkeyMenuOpened,"screenshot hotkey menu opens on host thread");
        Send(Screenshot,static_cast<Action>(HotkeyFirst+4));
        for(int i=0;i<Count;++i) {
            DWORD pid=0; GetWindowThreadProcessId(slots[i].window,&pid);
            TestCheck(pid==GetCurrentProcessId(),"module runs inside toolbox process");
        }
        TestCheck(GetParent(slots[Recording].window)==content[Recording],"recording page embedded");
        TestCheck(GetParent(slots[Countdown].window)==content[Countdown],"countdown page embedded");
        for(int i=0;i<6;++i) {
            Reveal(i); TestCheck((GetWindowLongPtrW(content[i],GWL_STYLE)&WS_VISIBLE)!=0,"page navigation");
            if(previews) {
                ShowWindow(host,SW_SHOWNOACTIVATE); UpdateWindow(host);
                TestCheck(SaveWindowPreview(host,DataDir(L"previews")+L"\\page-"+std::to_wstring(i)+L".png"),"window preview rendered");
            }
        }
        TestCheck(FindResourceW(nullptr,MAKEINTRESOURCEW(401),RT_RCDATA)==nullptr,"old ACE executable removed from distribution");
        TestCheck(closeToTray,"default close behavior is tray");
        SendMessageW(host,WM_CLOSE,0,0);
        TestCheck(!closing && !IsWindowVisible(host),"close button hides to tray by default");
        ShowWindow(host,SW_SHOW);
        SetCloseBehavior(false);
        wchar_t closeValue[16]{}; GetPrivateProfileStringW(L"general",L"close_action",L"",closeValue,16,Ini().c_str());
        TestCheck(wcscmp(closeValue,L"exit")==0,"direct-exit preference persisted");
        SetCloseBehavior(true);
        Send(Countdown,TestCountdown); Send(Recording,TestRecording); testPhase=1;
        testStart=GetTickCount();
    } else if (testPhase==1 && elapsed>6500) {
        Send(Recording,TestRecordingStop); Send(Countdown,Refresh); testPhase=2; testStart=GetTickCount();
    } else if (testPhase==2 && elapsed>2200) {
        TestCheck(screenshotPreset==4,"screenshot shortcut selection reaches module");
        TestCheck(status[Countdown].find(L"已完成")!=std::wstring::npos,"two-second countdown completed");
        bool video=false; std::error_code ec;
        for(auto& f:std::filesystem::directory_iterator(DataDir(L"test-recordings"),ec))
            if(f.path().extension()==L".mp4" && f.file_size()>4096 && f.last_write_time()>=testWallStart) video=true;
        TestCheck(video,"recording produced nonempty MP4");
        Send(KeepAlive,Stop); Send(League,Stop); testPhase=3; testStart=GetTickCount();
    } else if (testPhase==3 && elapsed>600) {
        if(slots[KeepAlive].running || slots[League].running) {
            if(elapsed>8000) { TestCheck(false,"background modules stop"); RequestClose(); }
            return;
        }
        TestCheck(true,"background modules stop");
        Start(KeepAlive); Start(League); testPhase=4; testStart=GetTickCount();
    } else if (testPhase==4 && elapsed>1000) {
        if((!slots[KeepAlive].window || !slots[League].window) && elapsed<8000) return;
        TestCheck(slots[KeepAlive].window && slots[League].window,"background modules restart");
        Command(330); Command(330); Command(340); Command(340);
        testPhase=5; testStart=GetTickCount();
    } else if (testPhase==5 && elapsed>1000) {
        bool ready=!moduleStopping[KeepAlive] && !moduleStopping[League] &&
            slots[KeepAlive].window && slots[League].window;
        if(!ready && elapsed<8000) return;
        TestCheck(ready,"rapid off-on toggles reconcile to running");
        Send(Screenshot,Configure); Send(League,Configure); testPhase=6; testStart=GetTickCount();
    } else if (testPhase==6 && elapsed>1000) {
        bool settings=false;
        EnumThreadWindows(slots[Screenshot].threadId,[](HWND h,LPARAM data)->BOOL {
            wchar_t cls[80]{}; GetClassNameW(h,cls,80);
            if(wcscmp(cls,L"JianJieSettings")==0) *reinterpret_cast<bool*>(data)=true;
            return TRUE;
        },reinterpret_cast<LPARAM>(&settings));
        TestCheck(settings,"screenshot settings open before shutdown");
        bool nativeDialog=false;
        EnumThreadWindows(slots[League].threadId,[](HWND h,LPARAM data)->BOOL {
            wchar_t cls[80]{}; GetClassNameW(h,cls,80);
            if(wcscmp(cls,L"#32770")==0) *reinterpret_cast<bool*>(data)=true;
            return TRUE;
        },reinterpret_cast<LPARAM>(&nativeDialog));
        TestCheck(nativeDialog,"native League dialog open before shutdown");
        SetCloseBehavior(false); SendMessageW(host,WM_CLOSE,0,0);
        TestCheck(closing,"direct-exit close starts graceful shutdown");
    }
}
void Command(int id) {
    if(closing) return;
    if(id>=NavBase && id<NavBase+6) { ShowHost(id-NavBase); return; }
    switch(id) {
    case 900: ShowHost(); break;
    case 300: ShowWindow(host,SW_HIDE); SetTimer(host,3,180,nullptr); break;
    case 301: ScreenshotHotkeyMenu(); break;
    case 302: Send(Screenshot,Configure); break;
    case 330:
        keepEnabled=!keepEnabled; SaveFlag(L"keepalive",keepEnabled);
        if(keepEnabled) { if(!moduleStopping[KeepAlive]) Start(KeepAlive); }
        else { moduleStopping[KeepAlive]=true; Send(KeepAlive,Stop); }
        SetWindowTextW(enableKeep,keepEnabled?L"关闭后台保活":L"开启后台保活"); break;
    case 331: Send(KeepAlive,Primary); break;
    case 332: Send(KeepAlive,Secondary); break;
    case 333: Send(KeepAlive,Configure); break;
    case 340:
        leagueEnabled=!leagueEnabled; SaveFlag(L"league",leagueEnabled);
        if(leagueEnabled) { if(!moduleStopping[League]) Start(League); }
        else { moduleStopping[League]=true; Send(League,Stop); }
        SetWindowTextW(enableLeague,leagueEnabled?L"关闭 LOL 助手":L"开启 LOL 助手"); break;
    case 341: Send(League,Primary); break;
    case 342: Send(League,Secondary); break;
    case 343: Send(League,Configure); break;
    case 344: ShellExecuteW(host,L"open",L"notepad.exe",L"D:\\LOL.txt",nullptr,SW_SHOW); break;
    case 350:
        if(!aceEnabled && !IsElevated() && !testMode) {
            WritePrivateProfileStringW(L"ace",L"enabled",L"1",Ini().c_str());
            restartElevated=true; RequestClose();
        } else Send(AceOptimizer,Primary);
        break;
    case 351: Send(AceOptimizer,Secondary); break;
    case 352: if(!IsElevated() && !testMode) { restartElevated=true; RequestClose(); } break;
    case 353: Send(AceOptimizer,Configure); break;
    case 354: Send(AceOptimizer,IoOption); break;
    case 355: Send(AceOptimizer,MemoryOption); break;
    case 1201: ToggleStartup(); break;
    case 1202: { auto dir=DataDir(); ShellExecuteW(host,L"open",dir.c_str(),nullptr,nullptr,SW_SHOW); break; }
    case 1203: RequestClose(); break;
    case 1204: CloseBehaviorMenu(); break;
    case 1210: SetCloseBehavior(true); break;
    case 1211: SetCloseBehavior(false); break;
    }
}
LRESULT CALLBACK WindowProc(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
    if(taskbarCreated && message==taskbarCreated) { AddTray(); return 0; }
    switch(message) {
    case WM_HOST_CALL: (*reinterpret_cast<const std::function<void()>*>(l))(); return 0;
    case WM_INITMENUPOPUP: if(reinterpret_cast<HMENU>(w)==hotkeyMenu) hotkeyMenuOpened=true; break;
    case WM_COMMAND: Command(LOWORD(w)); return 0;
    case WM_SIZE:
        if(w==SIZE_MINIMIZED) ShowWindow(hwnd,SW_HIDE); else Layout(); return 0;
    case WM_GETMINMAXINFO: {
        auto m=reinterpret_cast<MINMAXINFO*>(l); m->ptMinTrackSize={Scale(1120),Scale(800)}; return 0;
    }
    case WM_DPICHANGED: {
        dpi=HIWORD(w); auto r=reinterpret_cast<RECT*>(l);
        SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE); Layout(); return 0;
    }
    case WM_DRAWITEM: {
        auto d=reinterpret_cast<DRAWITEMSTRUCT*>(l);
        if(d->CtlID<NavBase || d->CtlID>=NavBase+6) break;
        bool active=(static_cast<int>(d->CtlID)-NavBase)==selected;
        HBRUSH brush=CreateSolidBrush(active?RGB(225,235,251):RGB(244,246,249));
        FillRect(d->hDC,&d->rcItem,brush); DeleteObject(brush);
        SetBkMode(d->hDC,TRANSPARENT); SetTextColor(d->hDC,active?RGB(30,84,158):RGB(64,76,94));
        SelectObject(d->hDC,font); RECT rect=d->rcItem; rect.left+=Scale(8);
        DrawTextW(d->hDC,Names[d->CtlID-NavBase],-1,&rect,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        if(d->itemState&ODS_FOCUS) DrawFocusRect(d->hDC,&d->rcItem);
        return TRUE;
    }
    case WM_CTLCOLORSTATIC: {
        auto dc=reinterpret_cast<HDC>(w); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(47,59,76));
        HWND c=reinterpret_cast<HWND>(l);
        return reinterpret_cast<LRESULT>(GetParent(c)==hwnd?background:white);
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{}; HDC dc=BeginPaint(hwnd,&ps); RECT r{}; GetClientRect(hwnd,&r);
        FillRect(dc,&r,background); SelectObject(dc,titleFont); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(32,49,70));
        RECT brand{Scale(8),Scale(12),Scale(104),Scale(50)}; DrawTextW(dc,L"工具箱",-1,&brand,DT_LEFT|DT_SINGLELINE);
        EndPaint(hwnd,&ps); return 0;
    }
    case WM_TIMER:
        if(w==3) { KillTimer(hwnd,3); Send(Screenshot,Primary); return 0; }
        if(closing) {
            if(AllStopped()) {
                for(auto& s:slots) if(s.thread.joinable()) s.thread.join();
                if(testMode) {
                    TestCheck(true,"all module threads stopped gracefully");
                    std::ofstream out(std::filesystem::path(DataDir())/L"integration-results.txt");
                    for(auto& line:testResults) out<<line<<'\n';
                }
                if(aceRestoreFailures && !testMode) MessageBoxW(host,
                    L"部分 ACE 进程拒绝恢复优先级，详情已记录到工具箱日志。请在游戏结束后正常重启相关程序以恢复默认值。",
                    L"ACE 优先级恢复结果",MB_ICONWARNING);
                DestroyWindow(hwnd);
            }
            return 0;
        }
        for(int i=0;i<Count;++i) if(slots[i].running) Send(static_cast<Module>(i),Refresh);
        for(int i : {int(KeepAlive),int(League)}) {
            if(moduleStopping[i] && !slots[i].running) {
                moduleStopping[i]=false;
                if(i==KeepAlive ? keepEnabled : leagueEnabled) Start(static_cast<Module>(i));
            }
        }
        TickTest(); return 0;
    case WM_STATE: {
        std::unique_ptr<State> state(reinterpret_cast<State*>(l));
        if(status[state->module]==state->text) return 0;
        status[state->module]=state->text;
        if(state->module==KeepAlive) SetWindowTextW(GetDlgItem(content[3],2301),state->text.c_str());
        if(state->module==League) SetWindowTextW(GetDlgItem(content[4],2401),state->text.c_str());
        if(state->module==AceOptimizer) {
            SetWindowTextW(GetDlgItem(content[5],2502),state->text.c_str());
            SetWindowTextW(aceLaunch,aceEnabled?L"停止优化并恢复原值":L"启用游戏优先优化");
            SendMessageW(GetDlgItem(content[5],353),BM_SETCHECK,aceLowCpu?BST_CHECKED:BST_UNCHECKED,0);
            SendMessageW(GetDlgItem(content[5],354),BM_SETCHECK,aceLowIo?BST_CHECKED:BST_UNCHECKED,0);
            SendMessageW(GetDlgItem(content[5],355),BM_SETCHECK,aceLowMemory?BST_CHECKED:BST_UNCHECKED,0);
        }
        if(selected==state->module && !closing) SetWindowTextW(footer,state->text.substr(0,state->text.find(L"\r\n")).c_str());
        return 0;
    }
    case WM_READY: Layout(); return 0;
    case WM_REVEAL: ShowHost(static_cast<int>(w)); return 0;
    case WM_BALLOON: {
        std::unique_ptr<Notice> notice(reinterpret_cast<Notice*>(l));
        if(testMode || closing) return 0;
        auto icon=tray; icon.uFlags=NIF_INFO; icon.dwInfoFlags=notice->flags;
        wcsncpy_s(icon.szInfoTitle,notice->title.c_str(),_TRUNCATE); wcsncpy_s(icon.szInfo,notice->text.c_str(),_TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY,&icon); return 0;
    }
    case TrayMessage:
        if(l==WM_LBUTTONUP || l==WM_LBUTTONDBLCLK) ShowHost();
        if(l==WM_RBUTTONUP) TrayMenu(); return 0;
    case WM_CLOSE:
        if(closeToTray) ShowWindow(hwnd,SW_HIDE); else RequestClose();
        return 0;
    case WM_QUERYENDSESSION: RequestClose(); return TRUE;
    case WM_DESTROY:
        KillTimer(hwnd,1); Shell_NotifyIconW(NIM_DELETE,&tray); PostQuitMessage(testMode?testFailures:0); return 0;
    }
    return DefWindowProcW(hwnd,message,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR command,int) {
    testMode=wcsstr(command,L"--self-test")!=nullptr;
    previews=wcsstr(command,L"--preview")!=nullptr;
    testWallStart=std::filesystem::file_time_type::clock::now();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HANDLE mutex=CreateMutexW(nullptr,FALSE,testMode?L"Local\\CppToolbox.Test.1":L"Local\\CppToolbox.1");
    if(!mutex) return 1;
    if(GetLastError()==ERROR_ALREADY_EXISTS) {
        HWND existing=FindWindowW(MainClass(),nullptr); if(existing) PostMessageW(existing,WM_REVEAL,-1,0);
        CloseHandle(mutex); return 0;
    }
    try {
        Log(L"toolbox starting");
        wchar_t closeValue[16]{};
        GetPrivateProfileStringW(L"general",L"close_action",L"tray",closeValue,16,Ini().c_str());
        closeToTray=testMode || wcscmp(closeValue,L"exit")!=0;
        {
            auto alarm=std::filesystem::path(DataDir(L"countdown"))/L"alarm.wav";
            if(!std::filesystem::exists(alarm)) {
                auto resource=FindResourceW(nullptr,MAKEINTRESOURCEW(402),RT_RCDATA);
                if(resource) {
                    auto bytes=LockResource(LoadResource(nullptr,resource));
                    std::ofstream out(alarm,std::ios::binary);
                    out.write(static_cast<const char*>(bytes),SizeofResource(nullptr,resource));
                }
            }
        }
        dpi=GetDpiForSystem();
        font=CreateFontW(-Scale(15),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
        titleFont=CreateFontW(-Scale(28),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
        smallFont=CreateFontW(-Scale(12),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
        background=CreateSolidBrush(RGB(244,246,249)); white=CreateSolidBrush(RGB(255,255,255));
        WNDCLASSW wc{}; wc.hInstance=instance; wc.lpfnWndProc=WindowProc; wc.lpszClassName=MainClass();
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1)); wc.hbrBackground=background;
        RegisterClassW(&wc);
        RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
        int width=Scale(1180), height=Scale(860);
        width=std::min(width,static_cast<int>(work.right-work.left)); height=std::min(height,static_cast<int>(work.bottom-work.top));
        host=CreateWindowExW(WS_EX_CONTROLPARENT,MainClass(),L"工具箱",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
            work.left+(work.right-work.left-width)/2,work.top+(work.bottom-work.top-height)/2,width,height,nullptr,nullptr,instance,nullptr);
        if(!host) { CloseHandle(mutex); return 2; }
        BuildPages(); Layout(); Reveal(wcsstr(command,L"--ace-page")?5:0);
        taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated"); AddTray();
        Start(Screenshot); Start(Recording); Start(Countdown); Start(AceOptimizer);
        keepEnabled=testMode || GetPrivateProfileIntW(L"modules",L"keepalive",0,Ini().c_str())!=0;
        leagueEnabled=testMode || GetPrivateProfileIntW(L"modules",L"league",0,Ini().c_str())!=0;
        if(keepEnabled) { Start(KeepAlive); SetWindowTextW(enableKeep,L"关闭后台保活"); }
        if(leagueEnabled) { Start(League); SetWindowTextW(enableLeague,L"关闭 LOL 助手"); }
        if(!testMode && !wcsstr(command,L"--tray")) ShowWindow(host,SW_SHOW);
        SetTimer(host,1,500,nullptr); testStart=GetTickCount();
        MSG msg{};
        while(GetMessageW(&msg,nullptr,0,0)>0) {
            // Embedded modules have their own message threads and keyboard handling.
            if(CountdownDialogMessage(&msg)) continue;
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        MSG pending{};
        while(PeekMessageW(&pending,nullptr,WM_STATE,WM_BALLOON,PM_REMOVE)) {
            if(pending.message==WM_STATE) delete reinterpret_cast<State*>(pending.lParam);
            if(pending.message==WM_BALLOON) delete reinterpret_cast<Notice*>(pending.lParam);
        }
        DeleteObject(font); DeleteObject(titleFont); DeleteObject(smallFont); DeleteObject(background); DeleteObject(white);
        CloseHandle(mutex); Log(L"toolbox stopped");
        if(restartElevated) {
            wchar_t path[32768]{}; GetModuleFileNameW(nullptr,path,32768);
            auto result=reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"runas",path,L"--ace-page",nullptr,SW_SHOW));
            if(result<=32) {
                WritePrivateProfileStringW(L"ace",L"enabled",L"0",Ini().c_str());
                ShellExecuteW(nullptr,L"open",path,L"--ace-page",nullptr,SW_SHOW);
            }
        }
        return static_cast<int>(msg.wParam);
    } catch(const std::exception&) {
        Log(L"fatal host exception");
        MessageBoxW(nullptr,L"工具箱初始化失败，请检查用户目录权限和日志。",L"工具箱",MB_ICONERROR);
        for(int i=0;i<Count;++i) Send(static_cast<Module>(i),Stop);
        for(auto& s:slots) if(s.thread.joinable()) s.thread.join();
        CloseHandle(mutex); return 3;
    }
}
