#include "history.h"
#include <algorithm>
#include <map>
#include <cwctype>
namespace {
std::wstring Key(std::wstring value) {
    std::transform(value.begin(),value.end(),value.begin(),[](wchar_t c){return c==L'/'?L'\\':std::towlower(c);});
    return value;
}
}
std::wstring CanonicalVideoPath(const std::wstring& path) {
    wchar_t expanded[32768]{}, full[32768]{};
    DWORD size=ExpandEnvironmentStringsW(path.c_str(),expanded,32768);
    if(path.empty() || !size || size>32768) return {};
    size=GetFullPathNameW(expanded,32768,full,nullptr);
    if(!size || size>=32768) return {};
    return full;
}
std::vector<RecItem> FolderVideos(const std::wstring& directory,const std::vector<RecItem>& recorded) {
    std::wstring dir=CanonicalVideoPath(directory); if(dir.empty()) return {};
    if(dir.back()!=L'\\') dir+=L'\\';
    std::map<std::wstring,long long> durations;
    for(auto& item:recorded) durations[Key(CanonicalVideoPath(item.path))]=item.durationMs;
    std::vector<RecItem> result;
    WIN32_FIND_DATAW data{}; HANDLE find=FindFirstFileW((dir+L"*").c_str(),&data);
    if(find==INVALID_HANDLE_VALUE) return result;
    do {
        if(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring name=data.cFileName; auto dot=name.find_last_of(L'.');
        if(dot==std::wstring::npos) continue;
        auto ext=Key(name.substr(dot));
        if(ext!=L".mp4" && ext!=L".mkv" && ext!=L".avi" && ext!=L".mov" && ext!=L".wmv" && ext!=L".webm" && ext!=L".m4v") continue;
        RecItem item; item.name=name; item.path=dir+name;
        item.bytes=(static_cast<long long>(data.nFileSizeHigh)<<32)|data.nFileSizeLow;
        ULARGE_INTEGER time{}; time.LowPart=data.ftLastWriteTime.dwLowDateTime; time.HighPart=data.ftLastWriteTime.dwHighDateTime;
        item.epoch=static_cast<long long>(time.QuadPart/10000000)-11644473600LL;
        auto known=durations.find(Key(item.path)); item.durationMs=known==durations.end()?-1:known->second;
        result.push_back(std::move(item));
    } while(FindNextFileW(find,&data));
    FindClose(find);
    std::sort(result.begin(),result.end(),[](auto& a,auto& b){return a.epoch!=b.epoch?a.epoch>b.epoch:Key(a.name)<Key(b.name);});
    return result;
}
