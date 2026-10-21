#include "ace_governor.h"
#include <windows.h>
#include <cstdio>
#include <string>
int failures=0;
void Check(bool value,const char* name) { printf("%s %s\n",value?"PASS":"FAIL",name); if(!value) ++failures; }
int wmain(int argc,wchar_t** argv) {
    if(argc>1) {
        HANDLE done=OpenEventW(SYNCHRONIZE,FALSE,argv[1]);
        if(done) { WaitForSingleObject(done,15000); CloseHandle(done); }
        return 0;
    }
    HANDLE token{};
    if(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY|TOKEN_ADJUST_PRIVILEGES,&token)) {
        LUID luid{}; LookupPrivilegeValueW(nullptr,SE_DEBUG_NAME,&luid);
        TOKEN_PRIVILEGES disabled{},old{}; disabled.PrivilegeCount=1; disabled.Privileges[0]={luid,0};
        DWORD length=sizeof(old); SetLastError(0);
        bool assigned=AdjustTokenPrivileges(token,FALSE,&disabled,sizeof(old),&old,&length) && GetLastError()==0;
        if(assigned) {
            PRIVILEGE_SET required{}; required.PrivilegeCount=1; required.Control=PRIVILEGE_SET_ALL_NECESSARY;
            required.Privilege[0]={luid,SE_PRIVILEGE_ENABLED}; BOOL enabled=FALSE;
            { ace::SchedulingPrivilege access(true); PrivilegeCheck(token,&required,&enabled); Check(access.Active() && enabled,"disabled administrator scheduling privilege enabled"); }
            PrivilegeCheck(token,&required,&enabled); Check(!enabled,"previous disabled scheduling privilege restored");
            if(old.PrivilegeCount) AdjustTokenPrivileges(token,FALSE,&old,0,nullptr,nullptr);
        } else printf("SKIP current token has no scheduling privilege to enable\n");
        CloseHandle(token);
    }
    Check(ace::IsCandidateName(L"ACE-Service64.exe"),"ACE service matched");
    Check(ace::IsCandidateName(L"ace-base.exe"),"case insensitive ACE match");
    Check(!ace::IsCandidateName(L"AceGentleLimiter.exe"),"old limiter never treated as ACE");
    Check(!ace::IsCandidateName(L"League of Legends.exe"),"game excluded");
    Check(!ace::IsCandidateName(L"notepad.exe"),"unrelated process excluded");
    Check(!ace::IsInstalledAcePath(L"D:\\Downloads\\ace\\ACE-Service.exe"),"arbitrary ace folder rejected");
    Check(!ace::IsInstalledAcePath(L"D:\\Downloads\\AntiCheatExpert\\ACE-Service64.exe"),"arbitrary anticheat folder rejected");
    wchar_t programFiles[32768]{}; GetEnvironmentVariableW(L"ProgramFiles",programFiles,32768);
    Check(ace::IsInstalledAcePath(std::wstring(programFiles)+L"\\AntiCheatExpert\\ACE-Service64.exe"),"installed root and known component accepted");
    Check(!ace::IsInstalledAcePath(std::wstring(programFiles)+L"\\AntiCheatExpertFake\\ACE-Service64.exe"),"installed root sibling rejected");
    Check(!ace::IsInstalledAcePath(std::wstring(programFiles)+L"\\AntiCheatExpert\\notepad.exe"),"unrelated executable inside root rejected");
    std::wstring eventName=L"Local\\CppToolbox.AceTest."+std::to_wstring(GetCurrentProcessId());
    HANDLE done=CreateEventW(nullptr,TRUE,FALSE,eventName.c_str());
    wchar_t exe[32768]{}; GetModuleFileNameW(nullptr,exe,32768);
    std::wstring command=L"\""+std::wstring(exe)+L"\" "+eventName;
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    Check(CreateProcessW(exe,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE,"test child launched");
    if(!process.hProcess) { CloseHandle(done); return 1; }
    DWORD original=GetPriorityClass(process.hProcess);
    MEMORY_PRIORITY_INFORMATION originalMemory{};
    Check(GetProcessInformation(process.hProcess,ProcessMemoryPriority,&originalMemory,sizeof(originalMemory))!=FALSE,"original memory page priority read");
    ULONG originalIo=0; DWORD error=0;
    bool ioSupported=ace::ReadIoPriority(process.hProcess,originalIo,error);
    {
        ace::Policy policy(process.hProcess);
        auto applied=policy.Apply(true,true,true);
        Check(applied.cpuActive,"CPU priority lowered and verified");
        Check(GetPriorityClass(process.hProcess)==BELOW_NORMAL_PRIORITY_CLASS,"CPU readback is below normal");
        Check(ioSupported && applied.ioActive,"I/O priority lowered and verified");
        Check(applied.memoryActive,"memory page priority lowered and verified");
        MEMORY_PRIORITY_INFORMATION memory{};
        Check(GetProcessInformation(process.hProcess,ProcessMemoryPriority,&memory,sizeof(memory)) && memory.MemoryPriority<=MEMORY_PRIORITY_BELOW_NORMAL,"memory page priority readback is below normal");
        ULONG io=0;
        Check(ace::ReadIoPriority(process.hProcess,io,error) && io<=1,"I/O readback is low");
        auto restored=policy.Restore();
        Check(restored,"explicit restore succeeded");
        Check(GetPriorityClass(process.hProcess)==original,"original CPU priority restored");
        Check(ace::ReadIoPriority(process.hProcess,io,error) && io==originalIo,"original I/O priority restored");
        Check(GetProcessInformation(process.hProcess,ProcessMemoryPriority,&memory,sizeof(memory)) && memory.MemoryPriority==originalMemory.MemoryPriority,"original memory page priority restored");
        policy.Apply(true,true,true);
    }
    Check(GetPriorityClass(process.hProcess)==original,"destructor restores CPU priority");
    ULONG io=0; Check(ace::ReadIoPriority(process.hProcess,io,error) && io==originalIo,"destructor restores I/O priority");
    MEMORY_PRIORITY_INFORMATION memory{};
    Check(GetProcessInformation(process.hProcess,ProcessMemoryPriority,&memory,sizeof(memory)) && memory.MemoryPriority==originalMemory.MemoryPriority,"destructor restores memory page priority");
    SetEvent(done); WaitForSingleObject(process.hProcess,5000);
    CloseHandle(process.hThread); CloseHandle(process.hProcess); CloseHandle(done);
    return failures?1:0;
}
