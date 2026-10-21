#include "history.h"
#include <filesystem>
#include <fstream>
#include <cstdio>
int main() {
    namespace fs=std::filesystem;
    auto root=fs::temp_directory_path()/(L"CppToolboxFolderTest"+std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(root/L"sub");
    fs::create_directories(root/L"other");
    for(auto name:{L"old.MP4",L"existing.mkv",L"ignore.txt",L"sub/nested.mp4",L"other/elsewhere.mp4"}) std::ofstream(root/name)<<"fixture";
    RecItem known; known.path=(root/L"old.MP4").wstring(); known.durationMs=12000;
    auto items=FolderVideos(root.wstring(),{known});
    int failures=0;
    auto check=[&](bool ok,const char* name){printf("%s %s\n",ok?"PASS":"FAIL",name); if(!ok)++failures;};
    check(items.size()==2,"existing videos discovered without history; nonvideos/subfolders excluded");
    bool duration=false; for(auto& item:items) if(item.name==L"old.MP4") duration=item.durationMs==12000 && item.bytes==7;
    check(duration,"known recording duration retained and disk size refreshed");
    known.path=(root/L"sub/../old.MP4").wstring(); known.durationMs=23000;
    auto normalized=FolderVideos((root/L"sub/..").wstring(),{known});
    bool normalizedDuration=false; for(auto& item:normalized) if(item.name==L"old.MP4") normalizedDuration=item.durationMs==23000;
    check(normalizedDuration,"noncanonical history path retains duration");
    fs::remove(root/L"existing.mkv");
    check(FolderVideos(root.wstring(),{}).size()==1,"external deletion reflected");
    check(FolderVideos((root/L"other").wstring(),{}).size()==1,"folder switching isolates files");
    check(FolderVideos((root/L"missing").wstring(),{}).empty(),"missing folder handled");
    for(auto file:{L"old.MP4",L"ignore.txt",L"sub/nested.mp4",L"other/elsewhere.mp4"}) fs::remove(root/file);
    fs::remove(root/L"sub"); fs::remove(root/L"other"); fs::remove(root);
    return failures?1:0;
}
