#pragma once
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <string>
#include <cstring>
namespace timer_session {
constexpr long long MaxDuration=1000LL*86400000;
inline long long Now() {return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
// Fixed-width, versioned local format. A complete snapshot is atomically replaced.
struct State {
    unsigned magic=0x31524D54,version=1;
    unsigned mode=0,alarm=0,popup=1,loop=1,flash=1;
    long long total=0,remaining=0,deadline=0;
    unsigned input[4]{};
    wchar_t sound[32768]{};
};
inline bool Valid(const State& s) {
    return s.magic==0x31524D54 && s.version==1 && s.mode>=1 && s.mode<=2 &&
        s.alarm<=1 && !(s.alarm && s.mode==2) && s.popup<=1 && s.loop<=1 && s.flash<=1 &&
        s.total>0 && s.total<=MaxDuration && s.remaining>=0 && s.remaining<=s.total &&
        (s.mode!=1 || (s.deadline>0 && s.deadline<=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::duration::max()).count())) &&
        s.input[0]<=999 && s.input[1]<=999 && s.input[2]<=999 && s.input[3]<=999 &&
        s.sound[32767]==0;
}
inline bool Save(const std::wstring& path,const State& s) {
    if(s.mode && !Valid(s))return false;
    auto temporary=path+L".tmp";
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;bool ok=WriteFile(file,&s,sizeof(s),&written,nullptr) && written==sizeof(s);
    if(ok)ok=FlushFileBuffers(file)!=FALSE;CloseHandle(file);
    if(ok)ok=MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok)DeleteFileW(temporary.c_str());return ok;
}
inline bool Load(const std::wstring& path,State& s) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    LARGE_INTEGER size{};DWORD read=0;State loaded{};
    bool ok=GetFileSizeEx(file,&size) && size.QuadPart==sizeof(loaded) && ReadFile(file,&loaded,sizeof(loaded),&read,nullptr) && read==sizeof(loaded);
    CloseHandle(file);if(!ok || !Valid(loaded))return false;s=loaded;return true;
}
inline long long Remaining(const State& s,long long now) {
    return s.mode==2?s.remaining:std::clamp(s.deadline-now,0LL,s.total);
}
}
