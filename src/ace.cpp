#include "ace.h"
#include "ace_governor.h"
#include "gateway.h"
#include <tlhelp32.h>
#include <map>
#include <memory>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cwctype>
#include <psapi.h>

namespace toolbox {
std::atomic<bool> aceEnabled{false}, aceLowCpu{true}, aceLowIo{true};
std::atomic<bool> aceLowMemory{true};
std::atomic<unsigned> aceRestoreFailures{0};
bool IsElevated() {
    HANDLE token{}; TOKEN_ELEVATION elevation{}; DWORD size=0;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) return false;
    BOOL okay=GetTokenInformation(token,TokenElevation,&elevation,sizeof(elevation),&size);
    CloseHandle(token); return okay && elevation.TokenIsElevated;
}
namespace {
struct Entry {
    DWORD pid=0, openError=0;
    std::wstring name, path;
    HANDLE process=nullptr;
    bool canSet=false;
    ULONGLONG openedAt=0;
    std::unique_ptr<ace::Policy> policy;
    bool verifiedPath=false, sampled=false;
    ULONGLONG tick=0, cpu=0, read=0, write=0;
    double cpuPercent=0, readRate=0, writeRate=0;
    double workingMb=0, privateMb=0;
    bool memorySampled=false;
    ace::Result result;
    ~Entry() { policy.reset(); if(process) CloseHandle(process); }
};
std::unique_ptr<Entry> Open(DWORD pid,const wchar_t* name) {
    auto row=std::make_unique<Entry>(); row->pid=pid; row->name=name;
    row->process=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_SET_INFORMATION|SYNCHRONIZE,FALSE,pid);
    row->canSet=row->process!=nullptr; row->openedAt=GetTickCount64();
    if(!row->process) {
        row->openError=GetLastError();
        row->process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);
    }
    if(row->process) {
        wchar_t path[32768]{}; DWORD count=32768;
        if(QueryFullProcessImageNameW(row->process,0,path,&count)) {
            row->path.assign(path,count); row->verifiedPath=ace::IsInstalledAcePath(row->path);
        }
        if(row->verifiedPath) row->policy=std::make_unique<ace::Policy>(row->process);
    }
    return row;
}
ULONGLONG Time(FILETIME value) { return (ULONGLONG(value.dwHighDateTime)<<32)|value.dwLowDateTime; }
void Sample(Entry& row) {
    if(!row.process) return;
    IO_COUNTERS io{}; FILETIME created{}, exited{}, kernel{}, user{};
    bool gotIo=GetProcessIoCounters(row.process,&io)!=FALSE;
    bool gotCpu=GetProcessTimes(row.process,&created,&exited,&kernel,&user)!=FALSE;
    ULONGLONG now=GetTickCount64(), cpu=Time(kernel)+Time(user);
    if(row.sampled && now>row.tick) {
        double ms=double(now-row.tick);
        if(gotCpu && cpu>=row.cpu) row.cpuPercent=double(cpu-row.cpu)/(ms*10000.0)*100.0/std::max<DWORD>(1,GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
        if(gotIo && io.ReadTransferCount>=row.read) row.readRate=double(io.ReadTransferCount-row.read)*1000.0/ms/1048576.0;
        if(gotIo && io.WriteTransferCount>=row.write) row.writeRate=double(io.WriteTransferCount-row.write)*1000.0/ms/1048576.0;
    }
    row.sampled=gotIo && gotCpu; row.tick=now; row.cpu=cpu;
    row.read=io.ReadTransferCount; row.write=io.WriteTransferCount;
    PROCESS_MEMORY_COUNTERS_EX memory{};
    row.memorySampled=GetProcessMemoryInfo(row.process,reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory))!=FALSE;
    if(row.memorySampled) { row.workingMb=memory.WorkingSetSize/1048576.0; row.privateMb=memory.PrivateUsage/1048576.0; }
}
struct Monitor {
    bool schedulingAccess=false;
    std::map<DWORD,std::unique_ptr<Entry>> entries;
    std::wstring summary=L"正在检测 ACE 进程…";
    bool RestoreAll() {
        unsigned failed=0;
        for(auto& [pid,row]:entries) if(row->policy && !row->policy->Restore()) {
            ++failed; Log(L"ACE restore failed pid="+std::to_wstring(pid)+L" error="+std::to_wstring(row->policy->RestoreError()));
        }
        aceRestoreFailures=failed; return failed==0;
    }
    void Scan() {
        if(testMode) {
            summary=L"自检模式：原生 ACE 优化模块就绪，未调整实际游戏进程。";
            StateText(AceOptimizer,summary); return;
        }
        for(auto it=entries.begin();it!=entries.end();) {
            if(it->second->process && WaitForSingleObject(it->second->process,0)==WAIT_OBJECT_0) it=entries.erase(it);
            else ++it;
        }
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
        if(snapshot==INVALID_HANDLE_VALUE) { StateText(AceOptimizer,L"无法读取进程列表，错误码 "+std::to_wstring(GetLastError())); return; }
        std::map<DWORD,bool> present;
        PROCESSENTRY32W item{}; item.dwSize=sizeof(item);
        if(Process32FirstW(snapshot,&item)) do {
            if(!ace::IsCandidateName(item.szExeFile)) continue;
            present[item.th32ProcessID]=true;
            auto found=entries.find(item.th32ProcessID);
            if(found==entries.end() || !found->second->process ||
               (!found->second->canSet && GetTickCount64()-found->second->openedAt>=15000))
                entries[item.th32ProcessID]=Open(item.th32ProcessID,item.szExeFile);
        } while(Process32NextW(snapshot,&item));
        CloseHandle(snapshot);
        for(auto it=entries.begin();it!=entries.end();) {
            if(!present.count(it->first)) it=entries.erase(it); else ++it;
        }
        unsigned cpuOkay=0, ioOkay=0, memoryOkay=0, denied=0;
        std::wostringstream details;
        details<<std::fixed<<std::setprecision(2);
        for(auto& [pid,row]:entries) {
            Sample(*row);
            if(row->policy) {
                row->result=row->policy->Apply(aceEnabled && aceLowCpu,aceEnabled && aceLowIo,aceEnabled && aceLowMemory);
                if(row->result.cpuActive) ++cpuOkay;
                if(row->result.ioActive) ++ioOkay;
                if(row->result.memoryActive) ++memoryOkay;
            }
            details<<L"\r\n"<<row->name<<L"  PID "<<pid<<L"\r\n";
            if(!row->verifiedPath) {
                ++denied;
                details<<(row->path.empty()?L"无法验证程序路径，未调整。权限/查询错误码 ":L"路径不属于已识别的 ACE 目录，未调整。错误码 ")<<row->openError<<L"\r\n";
                continue;
            }
            if(row->sampled) details<<L"CPU "<<row->cpuPercent<<L"%   读 "<<row->readRate<<L" MB/s   写 "<<row->writeRate<<L" MB/s\r\n";
            else details<<L"等待进程计数采样\r\n";
            if(row->memorySampled) details<<L"工作集 "<<row->workingMb<<L" MB   私有提交 "<<row->privateMb<<L" MB\r\n";
            else details<<L"内存计数暂不可读取\r\n";
            if(aceEnabled) {
                details<<L"CPU："<<(aceLowCpu?(row->result.cpuActive?L"低优先级已确认":L"未成功"):L"不调整");
                if(row->result.cpuError) details<<L"（错误码 "<<row->result.cpuError<<L"）";
                details<<L"   I/O："<<(aceLowIo?(row->result.ioActive?L"低优先级已确认":L"未成功"):L"不调整");
                if(row->result.ioError) details<<L"（错误码 "<<row->result.ioError<<L"）";
                details<<L"   内存："<<(aceLowMemory?(row->result.memoryActive?L"低页优先级已确认":L"未成功"):L"不调整");
                if(row->result.memoryError) details<<L"（错误码 "<<row->result.memoryError<<L"）";
                details<<L"\r\n";
                if(row->result.cpuError || row->result.ioError || row->result.memoryError) ++denied;
            } else if(row->policy && row->policy->HasChanges()) {
                details<<L"原值恢复失败；将继续尝试。错误码 "<<row->policy->RestoreError()<<L"\r\n";
            }
            details<<row->path<<L"\r\n";
        }
        std::wostringstream report;
        report<<(aceEnabled?L"游戏优先优化已开启":L"仅监测 · 优化未开启")<<L"  |  "<<(IsElevated()?L"管理员权限":L"普通权限")<<L"\r\n";
        if(IsElevated()) report<<(schedulingAccess?L"Windows 进程调度访问权限已启用。\r\n":L"Windows 进程调度访问权限未能启用，部分系统进程可能拒绝调整。\r\n");
        if(entries.empty()) report<<L"未发现 ACE 组件，等待游戏启动。\r\n";
        else report<<L"检测到 "<<entries.size()<<L" 个候选进程；CPU 低优先级 "<<cpuOkay<<L" 个，I/O 低优先级 "<<ioOkay<<L" 个，内存低页优先级 "<<memoryOkay<<L" 个。\r\n";
        MEMORYSTATUSEX systemMemory{sizeof(systemMemory)};
        if(GlobalMemoryStatusEx(&systemMemory)) report<<L"系统内存占用 "<<systemMemory.dwMemoryLoad<<L"%（内存紧张时按页优先级回收）\r\n";
        if(denied) report<<L"有 "<<denied<<L" 个进程未能完整调整或验证，详情见下方。\r\n";
        if(denied && !IsElevated()) report<<L"错误码 5 表示权限不足：请点击上方“以管理员权限重启”，在 Windows 提示中允许。\r\n";
        if(denied && IsElevated()) report<<L"错误码 5 表示目标仍拒绝访问；系统保护可能阻止调整，管理员身份不保证成功。\r\n";
        report<<details.str(); summary=report.str(); StateText(AceOptimizer,summary);
    }
};
thread_local Monitor* monitor=nullptr;
thread_local HWND window=nullptr;
void Save(const wchar_t* name,bool value) {
    WritePrivateProfileStringW(L"ace",name,value?L"1":L"0",(DataDir()+L"\\toolbox.ini").c_str());
}
void ActionHandler(Action action) {
    if(action==Primary) { aceEnabled=!aceEnabled; Save(L"enabled",aceEnabled); if(!aceEnabled) monitor->RestoreAll(); }
    if(action==Configure) { aceLowCpu=!aceLowCpu; Save(L"low_cpu",aceLowCpu); }
    if(action==IoOption) { aceLowIo=!aceLowIo; Save(L"low_io",aceLowIo); }
    if(action==MemoryOption) { aceLowMemory=!aceLowMemory; Save(L"low_memory",aceLowMemory); }
    if(action==Refresh) { StateText(AceOptimizer,monitor->summary); return; }
    if(action==Stop) {
        aceEnabled=false; monitor->RestoreAll();
        KillTimer(window,1); DestroyWindow(window); return;
    }
    monitor->Scan();
}
LRESULT CALLBACK WindowProc(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
    if(message==WM_TIMER) { monitor->Scan(); return 0; }
    if(message==WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd,message,w,l);
}
}
void RunAce() {
    ace::SchedulingPrivilege schedulingAccess(!testMode && IsElevated());
    Monitor state; state.schedulingAccess=schedulingAccess.Active(); monitor=&state;
    if(!testMode) Log(L"ACE scheduling privilege enabled="+std::to_wstring(schedulingAccess.Active())+L" error="+std::to_wstring(schedulingAccess.Error()));
    auto ini=DataDir()+L"\\toolbox.ini";
    aceEnabled=!testMode && GetPrivateProfileIntW(L"ace",L"enabled",0,ini.c_str())!=0;
    aceLowCpu=GetPrivateProfileIntW(L"ace",L"low_cpu",1,ini.c_str())!=0;
    aceLowIo=GetPrivateProfileIntW(L"ace",L"low_io",1,ini.c_str())!=0;
    aceLowMemory=GetPrivateProfileIntW(L"ace",L"low_memory",1,ini.c_str())!=0;
    WNDCLASSW wc{}; wc.hInstance=GetModuleHandleW(nullptr); wc.lpfnWndProc=WindowProc; wc.lpszClassName=L"CppToolbox.AceOptimizer";
    RegisterClassW(&wc);
    window=CreateWindowExW(0,wc.lpszClassName,L"ACE performance optimizer",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,nullptr);
    if(!window) return;
    Publish(AceOptimizer,window); SetTimer(window,1,3000,nullptr); state.Scan();
    MSG message{};
    while(NextMessage(&message,nullptr,0,0,ActionHandler)) { TranslateMessage(&message); DispatchMessageW(&message); }
    state.RestoreAll(); monitor=nullptr; window=nullptr;
    UnregisterClassW(wc.lpszClassName,wc.hInstance);
}
}
