#include "../src/modules/timer/alarm_time.h"
#include <iostream>
int main() {
    std::tm day{}; day.tm_year=126; day.tm_mon=9; day.tm_mday=21; day.tm_hour=12; day.tm_isdst=-1;
    auto now=std::mktime(&day);
    if(NextAlarm(now,12,0,1)-now!=1) return 1;
    if(NextAlarm(now,12,0,0)<=now) return 2;
    auto target=NextAlarm(now,11,30,0); std::tm local{}; localtime_s(&local,&target);
    if(local.tm_mday!=22 || local.tm_hour!=11 || local.tm_min!=30) return 3;
    try { NextAlarm(now,24,0,0); return 4; } catch(const std::invalid_argument&) {}
    std::cout<<"Alarm next occurrence and validation passed\n";
}
