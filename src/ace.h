#pragma once
#include <windows.h>
#include <string>
#include <atomic>
namespace toolbox {
extern std::atomic<bool> aceEnabled, aceLowCpu, aceLowIo, aceLowMemory;
extern std::atomic<unsigned> aceRestoreFailures;
bool IsElevated();
void RunAce();
}
