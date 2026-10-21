// Explicitly invoked live scheduling test; never registered with CTest.
#include "ace_governor.h"
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <vector>
#include <memory>
#include <string>
#include <algorithm>
#include <cwctype>
struct Process {
    DWORD pid; HANDLE handle; std::unique_ptr<ace::Policy> policy;
    DWORD cpu=0; ULONG io=0; bool ioKnown=false;
    IO_COUNTERS before{}, middle{};
};
int wmain(int argc,wchar_t** argv) {
    bool apply=argc>1 && std::wstring(argv[1])==L"--apply";
    printf("ACE live probe: %s; no game API, service control, termination or injection.\n",apply?"apply and restore":"observe only");
    HANDLE token{}; TOKEN_ELEVATION elevation{}; DWORD tokenSize=0;
    if(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) {
        GetTokenInformation(token,TokenElevation,&elevation,sizeof(elevation),&tokenSize); CloseHandle(token);
    }
    printf("elevated=%lu\n",elevation.TokenIsElevated);
    std::vector<Process> processes;
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE) return 1;
    PROCESSENTRY32W item{}; item.dwSize=sizeof(item);
    if(Process32FirstW(snapshot,&item)) do {
        if(!ace::IsCandidateName(item.szExeFile)) continue;
        HANDLE process=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_SET_INFORMATION|SYNCHRONIZE,FALSE,item.th32ProcessID);
        DWORD error=process?0:GetLastError();
        if(!process) process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,item.th32ProcessID);
        wchar_t image[32768]{}; DWORD length=32768;
        bool known=false;
        if(process && QueryFullProcessImageNameW(process,0,image,&length)) {
            known=ace::IsInstalledAcePath(std::wstring(image,length));
        }
        printf("pid=%lu full_open_error=%lu verified_ACE_path=%d\n",item.th32ProcessID,error,known);
        if(!process || !known) { if(process) CloseHandle(process); continue; }
        Process row{item.th32ProcessID,process,std::make_unique<ace::Policy>(process)};
        row.cpu=GetPriorityClass(process); DWORD ioError=0;
        row.ioKnown=ace::ReadIoPriority(process,row.io,ioError);
        GetProcessIoCounters(process,&row.before);
        printf("pid=%lu cpu_before=0x%lx io_before=%lu io_query_error=%lu\n",row.pid,row.cpu,row.io,ioError);
        processes.push_back(std::move(row));
    } while(Process32NextW(snapshot,&item));
    CloseHandle(snapshot);
    Sleep(3000);
    for(auto& row:processes) {
        GetProcessIoCounters(row.handle,&row.middle);
        printf("pid=%lu baseline_3s_read_bytes=%llu write_bytes=%llu\n",row.pid,
            row.middle.ReadTransferCount-row.before.ReadTransferCount,row.middle.WriteTransferCount-row.before.WriteTransferCount);
        if(apply) {
            auto result=row.policy->Apply(true,true);
            printf("pid=%lu cpu_low_verified=%d cpu_error=%lu io_low_verified=%d io_error=%lu\n",
                row.pid,result.cpuActive,result.cpuError,result.ioActive,result.ioError);
        }
    }
    Sleep(5000);
    int failures=0;
    for(auto& row:processes) {
        IO_COUNTERS after{}; GetProcessIoCounters(row.handle,&after);
        printf("pid=%lu following_5s_read_bytes=%llu write_bytes=%llu\n",row.pid,
            after.ReadTransferCount-row.middle.ReadTransferCount,after.WriteTransferCount-row.middle.WriteTransferCount);
        bool restored=row.policy->Restore(); DWORD cpu=GetPriorityClass(row.handle),error=0; ULONG io=0;
        bool ioKnown=ace::ReadIoPriority(row.handle,io,error);
        bool same=cpu==row.cpu && (!row.ioKnown || (ioKnown && io==row.io));
        printf("pid=%lu restore_ok=%d cpu_after=0x%lx io_after=%lu originals_match=%d restore_error=%lu\n",
            row.pid,restored,cpu,io,same,row.policy->RestoreError());
        if(!restored || !same) ++failures;
        row.policy.reset(); CloseHandle(row.handle);
    }
    printf("verified_process_count=%zu restore_failures=%d\n",processes.size(),failures);
    return failures?1:0;
}
