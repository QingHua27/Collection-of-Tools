#pragma once
#include <windows.h>
#include <string>

namespace tank {
enum class StartupMode { Disabled, Windows, FollowQQ, Unrecognized };
std::wstring StartupCommand(const std::wstring& executable, StartupMode mode);
StartupMode ParseStartup(const std::wstring& executable, const std::wstring& command);
class StartupStore {
public:
    explicit StartupStore(std::wstring executable,
        std::wstring subkey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run");
    StartupMode Read() const;
    LSTATUS Set(StartupMode mode) const;
private:
    std::wstring executable_, subkey_;
};
}
