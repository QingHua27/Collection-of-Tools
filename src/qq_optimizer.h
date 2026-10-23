#pragma once
#include <windows.h>
#include <string>
#include <cwctype>
#include <atomic>
namespace toolbox {
void RunQQOptimizer();
extern std::atomic<bool> qqEcoSupported;
namespace qq {
inline bool UnsupportedEcoError(DWORD error) {
    return error==ERROR_INVALID_PARAMETER || error==ERROR_NOT_SUPPORTED || error==ERROR_CALL_NOT_IMPLEMENTED;
}
inline bool SharedBrowser(const std::wstring& path) {
    auto slash=path.find_last_of(L"\\/");auto file=path.substr(slash==std::wstring::npos?0:slash+1);
    return _wcsicmp(file.c_str(),L"QQGameBrowser.exe")==0 || _wcsicmp(file.c_str(),L"QQMicroGameBox.exe")==0 || _wcsicmp(file.c_str(),L"CefBrowserProcess.exe")==0;
}
inline bool ShouldOptimize(bool enabled,bool foreground,bool browser,bool allowBrowser) {
    return enabled && !foreground && (!browser || allowBrowser);
}
inline bool Candidate(const std::wstring& path) {
    std::wstring p=path;for(auto& c:p)c=c==L'/'?L'\\':std::towlower(c);
    auto slash=p.find_last_of(L'\\');if(slash==std::wstring::npos)return false;
    auto file=p.substr(slash+1);
    if(file==L"qqmicrogamebox.exe" || file==L"cefbrowserprocess.exe") {
        wchar_t appdata[32768]{};if(!GetEnvironmentVariableW(L"APPDATA",appdata,32768))return false;
        std::wstring root=appdata;for(auto& c:root)c=std::towlower(c);root+=L"\\tencent\\qqmicrogamebox\\";
        return p.rfind(root,0)==0;
    }
    if(file!=L"qqgame.exe" && file!=L"qqgamebrowser.exe" && file!=L"qqgamex64.exe")return false;
    bool installed=p.find(L"\\program files (x86)\\tencent\\")!=std::wstring::npos || p.find(L"\\program files\\tencent\\")!=std::wstring::npos;
    return installed && (p.find(L"\\tencent\\qqgametempest\\hall.")!=std::wstring::npos ||
           p.find(L"\\tencent\\qqgame\\")!=std::wstring::npos);
}
class EcoPolicy {
    HANDLE process_;
    PROCESS_POWER_THROTTLING_STATE original_{};
    bool changed_=false;
    bool unsupported_=false;
public:
    explicit EcoPolicy(HANDLE process):process_(process){}
    ~EcoPolicy(){Restore();}
    bool Apply(DWORD& error) {
        if(unsupported_){error=ERROR_NOT_SUPPORTED;return false;}
        PROCESS_POWER_THROTTLING_STATE current{};current.Version=PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        if(!GetProcessInformation(process_,ProcessPowerThrottling,&current,sizeof(current))) {error=GetLastError();unsupported_=UnsupportedEcoError(error);return false;}
        if((current.ControlMask&current.StateMask&PROCESS_POWER_THROTTLING_EXECUTION_SPEED)!=0){error=0;return true;}
        if(!changed_)original_=current;
        current.ControlMask|=PROCESS_POWER_THROTTLING_EXECUTION_SPEED;current.StateMask|=PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        if(!SetProcessInformation(process_,ProcessPowerThrottling,&current,sizeof(current))) {error=GetLastError();return false;}
        changed_=true;
        PROCESS_POWER_THROTTLING_STATE verify{};verify.Version=PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        if(!GetProcessInformation(process_,ProcessPowerThrottling,&verify,sizeof(verify)) || !(verify.StateMask&PROCESS_POWER_THROTTLING_EXECUTION_SPEED)){error=ERROR_INVALID_DATA;return false;}
        error=0;return true;
    }
    bool Restore() {
        if(!changed_)return true;
        if(WaitForSingleObject(process_,0)==WAIT_OBJECT_0){changed_=false;return true;}
        PROCESS_POWER_THROTTLING_STATE current{};current.Version=PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        if(!GetProcessInformation(process_,ProcessPowerThrottling,&current,sizeof(current)))return false;
        auto bit=PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        current.ControlMask=(current.ControlMask&~bit)|(original_.ControlMask&bit);
        current.StateMask=(current.StateMask&~bit)|(original_.StateMask&bit);
        if(!SetProcessInformation(process_,ProcessPowerThrottling,&current,sizeof(current)))return false;
        changed_=false;return true;
    }
};
}
}
