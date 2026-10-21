#pragma once
#include <windows.h>
#include <string>

namespace ace {
class SchedulingPrivilege {
public:
    explicit SchedulingPrivilege(bool request);
    ~SchedulingPrivilege();
    SchedulingPrivilege(const SchedulingPrivilege&)=delete;
    SchedulingPrivilege& operator=(const SchedulingPrivilege&)=delete;
    bool Active() const { return active_; }
    DWORD Error() const { return error_; }
private:
    HANDLE token_=nullptr;
    TOKEN_PRIVILEGES previous_{};
    bool active_=false;
    DWORD error_=0;
};
bool IsCandidateName(const std::wstring& name);
bool IsInstalledAcePath(const std::wstring& path);
bool ReadIoPriority(HANDLE process, ULONG& priority, DWORD& error);
struct Result {
    bool cpuActive=false, ioActive=false, memoryActive=false;
    DWORD cpuError=0, ioError=0, memoryError=0;
};
// Borrows an already-open process handle; caller must keep it alive.
class Policy {
public:
    explicit Policy(HANDLE process): process_(process) {}
    ~Policy();
    Policy(const Policy&)=delete;
    Policy& operator=(const Policy&)=delete;
    Result Apply(bool cpu, bool io, bool memory=false);
    bool Restore();
    bool HasChanges() const { return cpuChanged_ || ioChanged_ || memoryChanged_; }
    DWORD RestoreError() const { return restoreError_; }
private:
    bool RestoreCpu();
    bool RestoreIo();
    bool RestoreMemory();
    HANDLE process_;
    DWORD originalCpu_=0;
    ULONG originalIo_=0;
    ULONG originalMemory_=0;
    bool cpuChanged_=false, ioChanged_=false, memoryChanged_=false;
    DWORD restoreError_=0;
};
}
