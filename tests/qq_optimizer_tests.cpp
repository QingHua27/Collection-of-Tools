#include "qq_optimizer.h"
#include "ace_governor.h"
#include <iostream>
int main() {
    using namespace toolbox::qq;
    auto check=[](bool ok){if(!ok)throw "QQ optimizer assertion failed";};
    try {
        check(Candidate(L"D:\\Program Files (x86)\\Tencent\\QQGameTempest\\Hall.59043\\QQGameBrowser.exe"));
        check(Candidate(L"C:\\Program Files\\Tencent\\QQGame\\QQGame.exe"));
        check(!Candidate(L"C:\\Users\\test\\AppData\\Roaming\\Tencent\\QQMicroGameBox\\QQMicroGameBox.exe"));
        check(!Candidate(L"D:\\Program Files (x86)\\Tencent\\QQGameTempest\\Hall.59043\\CefBrowserProcess.exe"));
        check(!Candidate(L"D:\\Downloads\\Tencent\\QQGame\\QQGame.exe"));
        check(SharedBrowser(L"D:\\Program Files (x86)\\Tencent\\QQGameTempest\\Hall.59043\\QQGameBrowser.exe"));
        check(!SharedBrowser(L"D:\\Program Files (x86)\\Tencent\\QQGameTempest\\Hall.59043\\QQGame.exe"));
        check(!ShouldOptimize(false,false,false,false));
        check(ShouldOptimize(true,false,false,false));
        check(!ShouldOptimize(true,true,false,true));
        check(!ShouldOptimize(true,true,true,true));
        check(!ShouldOptimize(true,false,true,false));
        check(ShouldOptimize(true,false,true,true));
        check(UnsupportedEcoError(ERROR_INVALID_PARAMETER));
        check(UnsupportedEcoError(ERROR_NOT_SUPPORTED));
        check(!UnsupportedEcoError(ERROR_ACCESS_DENIED));
        wchar_t appdata[32768]{};GetEnvironmentVariableW(L"APPDATA",appdata,32768);
        check(Candidate(std::wstring(appdata)+L"\\Tencent\\QQMicroGameBox\\4.1.4.2\\QQMicroGameBox.exe"));
        check(Candidate(std::wstring(appdata)+L"\\Tencent\\QQMicroGameBox\\4.1.4.2\\CefBrowserProcess.exe"));
        auto process=GetCurrentProcess();DWORD priority=GetPriorityClass(process);
        {ace::Policy policy(process);auto result=policy.Apply(true,false,true);check(result.cpuActive);check(policy.Restore());}
        check(GetPriorityClass(process)==priority);
        PROCESS_POWER_THROTTLING_STATE before{};before.Version=PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        if(GetProcessInformation(process,ProcessPowerThrottling,&before,sizeof(before))) {
            EcoPolicy eco(process);DWORD error=0;
            if(eco.Apply(error)) {
                check(eco.Restore());PROCESS_POWER_THROTTLING_STATE after{};after.Version=PROCESS_POWER_THROTTLING_CURRENT_VERSION;
                check(GetProcessInformation(process,ProcessPowerThrottling,&after,sizeof(after))!=FALSE);
                auto bit=PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
                check((before.ControlMask&bit)==(after.ControlMask&bit) && (before.StateMask&bit)==(after.StateMask&bit));
            } else check(error!=0);
        } else {
            DWORD unsupported=GetLastError();check(UnsupportedEcoError(unsupported));
            EcoPolicy eco(process);DWORD error=0;check(!eco.Apply(error) && UnsupportedEcoError(error));
            check(!eco.Apply(error) && error==ERROR_NOT_SUPPORTED);check(eco.Restore());
            std::cout<<"PASS unsupported EcoQoS read path leaves original state untouched\n";
        }
    } catch(...) {std::cerr<<"FAIL QQ hall eligibility or scheduling restoration\n";return 1;}
    std::cout<<"PASS hall matching, game exclusion, priority and EcoQoS restoration\n";
}
