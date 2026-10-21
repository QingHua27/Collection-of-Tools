#include "ace_governor.h"
#include <algorithm>
#include <cwctype>
#include <vector>

namespace ace {
SchedulingPrivilege::SchedulingPrivilege(bool request) {
    if(!request) return;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY|TOKEN_ADJUST_PRIVILEGES,&token_)) { error_=GetLastError(); return; }
    LUID luid{};
    if(!LookupPrivilegeValueW(nullptr,SE_DEBUG_NAME,&luid)) { error_=GetLastError(); return; }
    TOKEN_PRIVILEGES desired{}; desired.PrivilegeCount=1; desired.Privileges[0]={luid,SE_PRIVILEGE_ENABLED};
    DWORD size=sizeof(previous_); SetLastError(ERROR_SUCCESS);
    if(!AdjustTokenPrivileges(token_,FALSE,&desired,sizeof(previous_),&previous_,&size)) { error_=GetLastError(); return; }
    error_=GetLastError(); active_=error_==ERROR_SUCCESS;
}
SchedulingPrivilege::~SchedulingPrivilege() {
    if(token_) {
        if(active_ && previous_.PrivilegeCount) AdjustTokenPrivileges(token_,FALSE,&previous_,0,nullptr,nullptr);
        CloseHandle(token_);
    }
}
namespace {
// PHNT PROCESSINFOCLASS::ProcessIoPriority = 33, IO_PRIORITY_HINT low = 1.
// Resolve at runtime because this native API is not a stable Win32 contract.
using Query=LONG(NTAPI*)(HANDLE,ULONG,PVOID,ULONG,PULONG);
using Set=LONG(NTAPI*)(HANDLE,ULONG,PVOID,ULONG);
using ToDos=ULONG(WINAPI*)(LONG);
constexpr ULONG IoPriorityClass=33;
Query QueryFn() { static auto fn=reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryInformationProcess")); return fn; }
Set SetFn() { static auto fn=reinterpret_cast<Set>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtSetInformationProcess")); return fn; }
DWORD NativeError(LONG status) {
    static auto convert=reinterpret_cast<ToDos>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlNtStatusToDosError"));
    return convert?convert(status):ERROR_GEN_FAILURE;
}
bool SetIo(HANDLE process,ULONG value,DWORD& error) {
    auto fn=SetFn(); if(!fn) { error=ERROR_CALL_NOT_IMPLEMENTED; return false; }
    LONG status=fn(process,IoPriorityClass,&value,sizeof(value));
    if(status<0) { error=NativeError(status); return false; }
    error=0; return true;
}
bool AlreadyLow(DWORD priority) { return priority==IDLE_PRIORITY_CLASS || priority==BELOW_NORMAL_PRIORITY_CLASS; }
std::wstring Normalize(std::wstring path) {
    if(path.rfind(L"\\??\\",0)==0 || path.rfind(L"\\\\?\\",0)==0) path.erase(0,4);
    wchar_t full[32768]{};
    DWORD length=GetFullPathNameW(path.c_str(),32768,full,nullptr);
    if(!length || length>=32768) return {};
    path.assign(full,length);
    std::transform(path.begin(),path.end(),path.begin(),[](wchar_t c){return c==L'/'?L'\\':std::towlower(c);});
    return path;
}
}
bool IsCandidateName(const std::wstring& name) {
    std::wstring lower=name;
    std::transform(lower.begin(),lower.end(),lower.begin(),[](wchar_t c){return std::towlower(c);});
    // Exact known component names, not a broad substring match such as "ace".
    const wchar_t* names[]={L"ace-base.exe",L"ace-service.exe",L"ace-service64.exe",L"ace-tray.exe",L"ace-guard.exe",L"ace-guard64.exe",L"ace-helper.exe",L"sguard64.exe",L"sguardsvc64.exe",L"sguardupdate64.exe"};
    for(auto candidate:names) if(lower==candidate) return true;
    return false;
}
bool IsInstalledAcePath(const std::wstring& path) {
    const auto normalized=Normalize(path);
    auto slash=normalized.find_last_of(L'\\');
    if(slash==std::wstring::npos || !IsCandidateName(normalized.substr(slash+1))) return false;
    static const auto roots=[] {
        std::vector<std::wstring> result;
        for(auto variable:{L"ProgramFiles",L"ProgramFiles(x86)",L"ProgramW6432"}) {
            wchar_t folder[32768]{};
            if(GetEnvironmentVariableW(variable,folder,32768)) result.push_back(Normalize(std::wstring(folder)+L"\\AntiCheatExpert")+L"\\");
        }
        return result;
    }();
    for(auto& root:roots) if(normalized.rfind(root,0)==0) return true;
    // Nonstandard installations are accepted only when registered as actual ACE services.
    for(auto service:{L"AntiCheatExpert Protection",L"AntiCheatExpert Service"}) {
        auto key=std::wstring(L"SYSTEM\\CurrentControlSet\\Services\\")+service;
        wchar_t image[32768]{}; DWORD size=sizeof(image);
        if(RegGetValueW(HKEY_LOCAL_MACHINE,key.c_str(),L"ImagePath",RRF_RT_REG_SZ|RRF_RT_REG_EXPAND_SZ,nullptr,image,&size)!=ERROR_SUCCESS) continue;
        std::wstring executable=image;
        if(!executable.empty() && executable.front()==L'"') {
            auto end=executable.find(L'"',1); if(end==std::wstring::npos) continue;
            executable=executable.substr(1,end-1);
        } else {
            auto lower=executable;
            std::transform(lower.begin(),lower.end(),lower.begin(),[](wchar_t c){return std::towlower(c);});
            auto end=lower.find(L".exe"); if(end==std::wstring::npos) continue;
            executable.resize(end+4);
        }
        if(Normalize(executable)==normalized) return true;
    }
    return false;
}
bool ReadIoPriority(HANDLE process,ULONG& priority,DWORD& error) {
    auto fn=QueryFn(); if(!fn) { error=ERROR_CALL_NOT_IMPLEMENTED; return false; }
    LONG status=fn(process,IoPriorityClass,&priority,sizeof(priority),nullptr);
    if(status<0) { error=NativeError(status); return false; }
    error=0; return true;
}
Policy::~Policy() { Restore(); }
Result Policy::Apply(bool cpu,bool io,bool memory) {
    Result result;
    if(!cpu) { if(!RestoreCpu()) result.cpuError=restoreError_; }
    else {
        DWORD current=GetPriorityClass(process_);
        if(!current) result.cpuError=GetLastError();
        else if(AlreadyLow(current)) result.cpuActive=true;
        else {
            if(!cpuChanged_) originalCpu_=current;
            if(SetPriorityClass(process_,BELOW_NORMAL_PRIORITY_CLASS)) {
                cpuChanged_=true;
                result.cpuActive=AlreadyLow(GetPriorityClass(process_));
                if(!result.cpuActive) result.cpuError=ERROR_INVALID_DATA;
            } else result.cpuError=GetLastError();
        }
    }
    if(!io) { if(!RestoreIo()) result.ioError=restoreError_; }
    else {
        ULONG current=0; DWORD error=0;
        if(!ReadIoPriority(process_,current,error)) result.ioError=error;
        else if(current<=1) result.ioActive=true;
        else {
            if(!ioChanged_) originalIo_=current;
            if(SetIo(process_,1,error)) {
                ioChanged_=true;
                result.ioActive=ReadIoPriority(process_,current,error) && current<=1;
                if(!result.ioActive) result.ioError=error?error:ERROR_INVALID_DATA;
            } else result.ioError=error;
        }
    }
    if(!memory) { if(!RestoreMemory()) result.memoryError=restoreError_; }
    else {
        MEMORY_PRIORITY_INFORMATION current{};
        if(!GetProcessInformation(process_,ProcessMemoryPriority,&current,sizeof(current))) result.memoryError=GetLastError();
        else if(current.MemoryPriority<=MEMORY_PRIORITY_BELOW_NORMAL) result.memoryActive=true;
        else {
            if(!memoryChanged_) originalMemory_=current.MemoryPriority;
            MEMORY_PRIORITY_INFORMATION low{MEMORY_PRIORITY_BELOW_NORMAL};
            if(SetProcessInformation(process_,ProcessMemoryPriority,&low,sizeof(low))) {
                memoryChanged_=true;
                result.memoryActive=GetProcessInformation(process_,ProcessMemoryPriority,&current,sizeof(current)) && current.MemoryPriority<=MEMORY_PRIORITY_BELOW_NORMAL;
                if(!result.memoryActive) result.memoryError=ERROR_INVALID_DATA;
            } else result.memoryError=GetLastError();
        }
    }
    return result;
}
bool Policy::RestoreCpu() {
    if(!cpuChanged_) return true;
    DWORD current=GetPriorityClass(process_);
    if(!current) { restoreError_=GetLastError(); return false; }
    if(current!=BELOW_NORMAL_PRIORITY_CLASS) { cpuChanged_=false; return true; }
    if(!SetPriorityClass(process_,originalCpu_)) { restoreError_=GetLastError(); return false; }
    if(GetPriorityClass(process_)!=originalCpu_) { restoreError_=ERROR_INVALID_DATA; return false; }
    cpuChanged_=false; return true;
}
bool Policy::RestoreIo() {
    if(!ioChanged_) return true;
    ULONG current=0; DWORD error=0;
    if(!ReadIoPriority(process_,current,error)) { restoreError_=error; return false; }
    if(current!=1) { ioChanged_=false; return true; }
    if(!SetIo(process_,originalIo_,error)) { restoreError_=error; return false; }
    if(!ReadIoPriority(process_,current,error) || current!=originalIo_) { restoreError_=error?error:ERROR_INVALID_DATA; return false; }
    ioChanged_=false; return true;
}
bool Policy::RestoreMemory() {
    if(!memoryChanged_) return true;
    MEMORY_PRIORITY_INFORMATION current{};
    if(!GetProcessInformation(process_,ProcessMemoryPriority,&current,sizeof(current))) { restoreError_=GetLastError(); return false; }
    if(current.MemoryPriority!=MEMORY_PRIORITY_BELOW_NORMAL) { memoryChanged_=false; return true; }
    MEMORY_PRIORITY_INFORMATION original{originalMemory_};
    if(!SetProcessInformation(process_,ProcessMemoryPriority,&original,sizeof(original))) { restoreError_=GetLastError(); return false; }
    if(!GetProcessInformation(process_,ProcessMemoryPriority,&current,sizeof(current)) || current.MemoryPriority!=originalMemory_) { restoreError_=ERROR_INVALID_DATA; return false; }
    memoryChanged_=false; return true;
}
bool Policy::Restore() {
    restoreError_=0;
    if(WaitForSingleObject(process_,0)==WAIT_OBJECT_0) { cpuChanged_=false; ioChanged_=false; memoryChanged_=false; return true; }
    bool cpu=RestoreCpu(), io=RestoreIo(), memory=RestoreMemory(); return cpu && io && memory;
}
}
