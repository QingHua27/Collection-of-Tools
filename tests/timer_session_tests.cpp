#include "../src/modules/timer/timer_session.h"
#include <iostream>
int main() {
    using namespace timer_session;State s;s.mode=1;s.total=60000;s.remaining=60000;s.deadline=120000;
    auto check=[](bool ok){if(!ok)throw "timer session assertion failed";};
    try {
        check(Valid(s));check(Remaining(s,90000)==30000);check(Remaining(s,130000)==0);
        s.alarm=1;check(Remaining(s,119000)==1000);check(Remaining(s,120001)==0);
        s.alarm=0;s.mode=2;s.remaining=12345;check(Remaining(s,999999999)==12345);
        wchar_t folder[MAX_PATH]{},file[MAX_PATH]{};GetTempPathW(MAX_PATH,folder);GetTempFileNameW(folder,L"tmr",0,file);
        std::wstring path=file;check(Save(path,s));State loaded;check(Load(path,loaded));check(loaded.mode==2 && loaded.remaining==12345);
        s.mode=1;s.deadline=120000;s.alarm=1;wcscpy_s(s.sound,L"alarm.wav");s.popup=0;
        check(Save(path,s));check(Load(path,loaded));check(loaded.alarm && !loaded.popup && Remaining(loaded,120001)==0);
        State idle;check(Save(path,idle));check(!Load(path,loaded));
        HANDLE h=CreateFileW(file,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);DWORD bytes;WriteFile(h,"bad",3,&bytes,nullptr);CloseHandle(h);check(!Load(path,loaded));
        DeleteFileW(file);s.mode=2;s.alarm=1;check(!Valid(s));
    } catch(...) {std::cerr<<"FAIL timer session recovery\n";return 1;}
    std::cout<<"PASS running countdown, alarm, overdue, pause, atomic replacement, consumed and corrupt sessions\n";
}
