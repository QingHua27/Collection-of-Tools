#include "startup.h"
namespace tank {
namespace {
LSTATUS ReadCommand(HKEY key, std::wstring& value) {
    DWORD type = 0, bytes = 0;
    LSTATUS result = RegQueryValueExW(key, L"TankStormKeepAlive", nullptr, &type, nullptr, &bytes);
    if (result != ERROR_SUCCESS) return result;
    if (type != REG_SZ || bytes > 65536 || bytes % sizeof(wchar_t)) return ERROR_INVALID_DATA;
    std::wstring buffer(bytes / sizeof(wchar_t) + 1, L'\0');
    result = RegQueryValueExW(key, L"TankStormKeepAlive", nullptr, &type, reinterpret_cast<BYTE*>(buffer.data()), &bytes);
    if (result == ERROR_SUCCESS && type == REG_SZ) value = buffer.c_str();
    return result;
}
}
std::wstring StartupCommand(const std::wstring& executable, StartupMode mode) {
    if (mode == StartupMode::Disabled || mode == StartupMode::Unrecognized || executable.empty() ||
        executable.find_first_of(L"\"\r\n") != std::wstring::npos) return {};
    return L"\"" + executable + (mode == StartupMode::Windows ? L"\" --tray" : L"\" --watch-qq");
}
StartupMode ParseStartup(const std::wstring& executable, const std::wstring& command) {
    if (command.empty()) return StartupMode::Disabled;
    if (_wcsicmp(command.c_str(), StartupCommand(executable, StartupMode::Windows).c_str()) == 0) return StartupMode::Windows;
    if (_wcsicmp(command.c_str(), StartupCommand(executable, StartupMode::FollowQQ).c_str()) == 0) return StartupMode::FollowQQ;
    return StartupMode::Unrecognized;
}
StartupStore::StartupStore(std::wstring executable, std::wstring subkey) : executable_(executable), subkey_(subkey) {}
StartupMode StartupStore::Read() const {
    HKEY key = nullptr;
    const LSTATUS opened = RegOpenKeyExW(HKEY_CURRENT_USER, subkey_.c_str(), 0, KEY_QUERY_VALUE, &key);
    if (opened == ERROR_FILE_NOT_FOUND) return StartupMode::Disabled;
    if (opened != ERROR_SUCCESS) return StartupMode::Unrecognized;
    std::wstring value; const LSTATUS result = ReadCommand(key, value); RegCloseKey(key);
    if (result == ERROR_FILE_NOT_FOUND) return StartupMode::Disabled;
    return result == ERROR_SUCCESS ? ParseStartup(executable_, value) : StartupMode::Unrecognized;
}
LSTATUS StartupStore::Set(StartupMode mode) const {
    if (mode == StartupMode::Unrecognized) return ERROR_INVALID_PARAMETER;
    const auto command = StartupCommand(executable_, mode);
    if (mode != StartupMode::Disabled && command.empty()) return ERROR_INVALID_PARAMETER;
    HKEY key = nullptr;
    LSTATUS result = RegCreateKeyExW(HKEY_CURRENT_USER, subkey_.c_str(), 0, nullptr, 0,
        KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS) return result;
    std::wstring existing; result = ReadCommand(key, existing);
    if (result == ERROR_SUCCESS && ParseStartup(executable_, existing) == StartupMode::Unrecognized) result = ERROR_ALREADY_EXISTS;
    if (result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND) {
        if (mode == StartupMode::Disabled) {
            result = RegDeleteValueW(key, L"TankStormKeepAlive");
            if (result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS;
        } else result = RegSetValueExW(key, L"TankStormKeepAlive", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()), static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(key); return result;
}
}
