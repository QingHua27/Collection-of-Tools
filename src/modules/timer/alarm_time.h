#pragma once
#include <ctime>
#include <stdexcept>
inline std::time_t NextAlarm(std::time_t now, int hour, int minute, int second) {
    if(hour<0 || hour>23 || minute<0 || minute>59 || second<0 || second>59)
        throw std::invalid_argument("Invalid alarm time");
    std::tm local{}; localtime_s(&local,&now);
    local.tm_hour=hour; local.tm_min=minute; local.tm_sec=second; local.tm_isdst=-1;
    auto target=std::mktime(&local);
    if(target<=now) { ++local.tm_mday; local.tm_isdst=-1; target=std::mktime(&local); }
    return target;
}
