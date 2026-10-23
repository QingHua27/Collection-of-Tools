#include "qq_optimizer.h"
#include "gateway.h"
#include "ace_governor.h"
#include "modules/keepalive/keepalive.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <map>
#include <memory>
#include <sstream>
#include <iomanip>
#include <algorithm>
namespace toolbox {
std::atomic<bool> qqEcoSupported{true};
namespace {
struct Row {
    HANDLE process=nullptr;
    std::wstring path;
    DWORD accessError=0;
    std::unique_ptr<ace::Policy> scheduling;
    std::unique_ptr<qq::EcoPolicy> eco;
    ULONGLONG tick=0,cpu=0;
    double percent=0,working=0,privateMb=0;
    ace::Result result{};DWORD ecoError=0;bool ecoActive=false,restoreWarned=false,lastActive=false;
    ~Row(){eco.reset();scheduling.reset();if(process)CloseHandle(process);}
};
std::wstring Path(HANDLE process) {wchar_t path[32768]{};DWORD size=32768;return QueryFullProcessImageNameW(process,0,path,&size)?std::wstring(path,size):L"";}
bool ForegroundHall() {
    DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    bool hall=process && qq::Candidate(Path(process));if(process)CloseHandle(process);return hall;
}
bool GameSharesHall() {
    bool shared=false;
    EnumWindows([](HWND h,LPARAM value)->BOOL {
        wchar_t title[512]{};GetWindowTextW(h,title,512);if(!wcsstr(title,L"坦克风暴"))return TRUE;
        DWORD pid=0;GetWindowThreadProcessId(h,&pid);HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
        if(process && qq::Candidate(Path(process)))*reinterpret_cast<bool*>(value)=true;
        if(process)CloseHandle(process);return TRUE;
    },(LPARAM)&shared);return shared;
}
ULONGLONG T(FILETIME f){return (ULONGLONG(f.dwHighDateTime)<<32)|f.dwLowDateTime;}
struct Monitor {
    HWND window=nullptr;HWINEVENTHOOK hook=nullptr;bool enabled=false,ecoEnabled=true,browsers=true,autoTrim=true;
    ULONGLONG lastPulse=0;unsigned pulseOkay=0,pulseFailed=0;
    std::map<DWORD,std::unique_ptr<Row>> rows;
    std::wstring summary=L"正在检测 QQ 游戏大厅…",notice;
    bool Restore(Row& row) {
        bool a=!row.eco || row.eco->Restore(),b=!row.scheduling || row.scheduling->Restore();
        if((!a || !b) && !row.restoreWarned)Log(L"QQ hall scheduling restore failed: "+row.path);
        row.restoreWarned=!a || !b;return a && b;
    }
    void Sample(Row& row) {
        FILETIME created{},exited{},kernel{},user{};auto now=GetTickCount64();
        if(GetProcessTimes(row.process,&created,&exited,&kernel,&user)) {
            auto cpu=T(kernel)+T(user);
            if(row.tick && now>row.tick && cpu>=row.cpu)row.percent=double(cpu-row.cpu)/(now-row.tick)/10000.0*100/std::max<DWORD>(1,GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
            row.cpu=cpu;row.tick=now;
        }
        PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
        if(GetProcessMemoryInfo(row.process,(PROCESS_MEMORY_COUNTERS*)&memory,sizeof(memory))) {row.working=memory.WorkingSetSize/1048576.0;row.privateMb=memory.PrivateUsage/1048576.0;}
    }
    void Scan(bool trim=false) {
        if(testMode){summary=L"自检模式：QQ 大厅优化就绪，未修改真实游戏进程。";StateText(QQOptimizer,summary);return;}
        for(auto it=rows.begin();it!=rows.end();)if(WaitForSingleObject(it->second->process,0)==WAIT_OBJECT_0)it=rows.erase(it);else ++it;
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
        if(snapshot!=INVALID_HANDLE_VALUE) {
            if(Process32FirstW(snapshot,&entry))do {
                if(_wcsicmp(entry.szExeFile,L"QQGame.exe") && _wcsicmp(entry.szExeFile,L"QQGameX64.exe") && _wcsicmp(entry.szExeFile,L"QQGameBrowser.exe") && _wcsicmp(entry.szExeFile,L"QQMicroGameBox.exe") && _wcsicmp(entry.szExeFile,L"CefBrowserProcess.exe"))continue;
                if(rows.count(entry.th32ProcessID))continue;
                HANDLE process=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_SET_INFORMATION|SYNCHRONIZE,FALSE,entry.th32ProcessID);
                DWORD access=process?0:GetLastError();if(!process)process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,entry.th32ProcessID);
                if(!process)continue;auto path=Path(process);if(!qq::Candidate(path)){CloseHandle(process);continue;}
                auto row=std::make_unique<Row>();row->process=process;row->path=path;row->accessError=access;
                if(!access){row->scheduling=std::make_unique<ace::Policy>(process);row->eco=std::make_unique<qq::EcoPolicy>(process);}
                rows.emplace(entry.th32ProcessID,std::move(row));
            }while(Process32NextW(snapshot,&entry));CloseHandle(snapshot);
        }
        bool foreground=ForegroundHall(),sharedGame=GameSharesHall();unsigned failures=0,trimmed=0,trimFailed=0;
        double before=0,after=0,totalCpu=0,totalPrivate=0;
        std::wostringstream details;details<<std::fixed<<std::setprecision(1);
        for(auto& [pid,row]:rows) {
            bool protectedBrowser=qq::SharedBrowser(row->path) && !browsers;
            bool apply=qq::ShouldOptimize(enabled,foreground,qq::SharedBrowser(row->path),browsers);
            bool trimNow=trim || (autoTrim && apply && !row->lastActive);row->lastActive=apply;
            if(apply && row->scheduling) {
                row->result=row->scheduling->Apply(true,true,true);
                row->ecoActive=ecoEnabled && row->eco->Apply(row->ecoError);
                if(!ecoEnabled && !row->eco->Restore())++failures;
            } else if(!Restore(*row))++failures;
            Sample(*row);before+=row->working;
            if(trimNow && qq::ShouldOptimize(true,foreground,qq::SharedBrowser(row->path),browsers)) {
                HANDLE memory=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_SET_QUOTA,FALSE,pid);
                if(memory && WaitForSingleObject(row->process,0)==WAIT_TIMEOUT && Path(memory)==row->path && EmptyWorkingSet(memory))++trimmed;else ++trimFailed;
                if(memory)CloseHandle(memory);Sample(*row);
            }
            after+=row->working;totalCpu+=row->percent;totalPrivate+=row->privateMb;
            auto slash=row->path.find_last_of(L'\\');details<<row->path.substr(slash+1)<<L"  PID "<<pid<<L"\r\nCPU "<<row->percent<<L"%   工作集 "<<row->working<<L" MB   私有提交 "<<row->privateMb<<L" MB\r\n";
            if(row->accessError)details<<L"只读检测：无法调整（错误 "<<row->accessError<<L"）\r\n";
            else if(protectedBrowser)details<<L"共享浏览器：仅检测（未勾选纳入后台优化）\r\n";
            else if(apply) {
                details<<L"CPU "<<(row->result.cpuActive?L"低优先级":L"未成功")<<L" / I/O "<<(row->result.ioActive?L"低优先级":L"未成功")<<L" / 内存页 "<<(row->result.memoryActive?L"低优先级":L"未成功")<<L"\r\n";
                if(!qqEcoSupported || qq::UnsupportedEcoError(row->ecoError))details<<L"节能调度：当前系统不支持读取原状态，已跳过\r\n";
                else if(!ecoEnabled)details<<L"节能调度：关闭\r\n";
                else if(row->ecoActive)details<<L"节能调度：已启用并读回确认\r\n";
                else details<<L"节能调度：未成功（错误 "<<row->ecoError<<L"）\r\n";
            }
            details<<L"\r\n";
        }
        if(enabled && browsers && !foreground && GetTickCount64()-lastPulse>=15000) {
            lastPulse=GetTickCount64();pulseOkay=pulseFailed=0;
            for(const auto& target:tank::DiscoverTargets()) {auto result=tank::PulseTarget(target,(lastPulse/15000)%2!=0);if(result.queued)++pulseOkay;else ++pulseFailed;}
        }
        if(trim || trimmed || trimFailed) {std::wostringstream text;if(foreground)text<<L"大厅或游戏正在前台，跳过内存整理。";else text<<L"本次整理 "<<trimmed<<L" 个进程，失败 "<<trimFailed<<L"；工作集 "<<std::fixed<<std::setprecision(1)<<before<<L" → "<<after<<L" MB（私有提交不会因此释放）。";notice=text.str();}
        std::wostringstream text;text<<std::fixed<<std::setprecision(1)<<L"QQ 大厅："<<(enabled?(foreground?L"前台使用中，已恢复调度":L"后台优化已开启"):L"仅检测，优化未开启")<<L"\r\n识别 "<<rows.size()<<L" 个进程 · CPU "<<totalCpu<<L"% · 工作集 "<<after<<L" MB · 私有提交 "<<totalPrivate<<L" MB\r\n";
        if(failures)text<<L"恢复未成功 "<<failures<<L" 项，请查看日志。\r\n";
        if(!qqEcoSupported)text<<L"兼容模式：节能调度已跳过，CPU / I/O / 内存页优化仍可使用。\r\n";
        if(sharedGame && !foreground)text<<L"检测到游戏共享大厅宿主，按后台在线策略处理。\r\n";
        if(enabled && !foreground)text<<L"坦克风暴保活：最近成功 "<<pulseOkay<<L" 项，失败 "<<pulseFailed<<L" 项（0 项表示未识别到游戏窗口）。\r\n";
        if(!notice.empty())text<<notice<<L"\r\n";text<<L"\r\n"<<details.str();if(rows.empty())text<<L"未找到已支持安装目录内的大厅进程。";
        summary=text.str();StateText(QQOptimizer,summary);
    }
    void RestoreAll(){for(auto& [pid,row]:rows)if(!Restore(*row))Log(L"QQ hall restore failed pid="+std::to_wstring(pid));}
};
Monitor* monitor=nullptr;
void CALLBACK ForegroundEvent(HWINEVENTHOOK,DWORD,HWND,LONG,LONG,DWORD,DWORD) {
    if(monitor)PostMessageW(monitor->window,WM_APP+41,0,0);
}
void ActionHandler(Action action) {
    if(action==Primary)monitor->enabled=!monitor->enabled;
    if(action==Configure && qqEcoSupported)monitor->ecoEnabled=!monitor->ecoEnabled;
    if(action==MemoryOption)monitor->browsers=!monitor->browsers;
    if(action==IoOption)monitor->autoTrim=!monitor->autoTrim;
    if(action==Stop){if(monitor->hook){UnhookWinEvent(monitor->hook);monitor->hook=nullptr;}monitor->RestoreAll();KillTimer(monitor->window,1);DestroyWindow(monitor->window);return;}
    if(action==Refresh){StateText(QQOptimizer,monitor->summary);return;}
    monitor->Scan(action==Secondary);
}
LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l) {
    if(m==WM_TIMER || m==WM_APP+41){monitor->Scan();return 0;}
    if(m==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(h,m,w,l);
}
}
void RunQQOptimizer() {
    Monitor state;monitor=&state;WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"CppToolbox.QQOptimizer";wc.lpfnWndProc=Proc;RegisterClassW(&wc);
    PROCESS_POWER_THROTTLING_STATE power{};power.Version=PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    if(!GetProcessInformation(GetCurrentProcess(),ProcessPowerThrottling,&power,sizeof(power)) && qq::UnsupportedEcoError(GetLastError()))qqEcoSupported=false;
    state.ecoEnabled=qqEcoSupported;
    state.window=CreateWindowExW(0,wc.lpszClassName,L"QQ hall optimizer",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,nullptr);
    if(!state.window){monitor=nullptr;return;}Publish(QQOptimizer,state.window);SetTimer(state.window,1,1000,nullptr);
    if(!testMode)state.hook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,ForegroundEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    state.Scan();
    MSG msg{};while(NextMessage(&msg,nullptr,0,0,ActionHandler)){TranslateMessage(&msg);DispatchMessageW(&msg);}if(state.hook)UnhookWinEvent(state.hook);state.RestoreAll();monitor=nullptr;UnregisterClassW(wc.lpszClassName,wc.hInstance);
}
}
