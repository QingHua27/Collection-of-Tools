#include "startup.h"
#include <iostream>
#include <stdexcept>

void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    const std::wstring exe = L"D:\\游戏\\TankStormKeepAlive.exe";
    const std::wstring key = L"Software\\TankStormKeepAlive\\Tests\\" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    HKEY registry = nullptr; DWORD disposition = 0;
    try {
        Check(tank::StartupCommand(exe, tank::StartupMode::Windows) == L"\"D:\\游戏\\TankStormKeepAlive.exe\" --tray", "Windows startup must quote the real Unicode path");
        Check(tank::StartupCommand(exe, tank::StartupMode::FollowQQ) == L"\"D:\\游戏\\TankStormKeepAlive.exe\" --watch-qq", "Follow mode must start the lightweight watcher");
        Check(tank::StartupCommand(exe, tank::StartupMode::Disabled).empty(), "Disabled startup must have no command");
        Check(tank::ParseStartup(exe, L"\"D:\\游戏\\TankStormKeepAlive.exe\" --watch-qq") == tank::StartupMode::FollowQQ, "Follow mode parsing failed");
        Check(tank::ParseStartup(exe, L"\"D:\\other.exe\" --tray") == tank::StartupMode::Unrecognized, "Unrelated startup command must not be treated as this app");
        Check(RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &registry, &disposition) == ERROR_SUCCESS && disposition == REG_CREATED_NEW_KEY, "test requires a fresh private registry key");
        const wchar_t* marker = L"keep unrelated values";
        Check(RegSetValueExW(registry, L"OtherApplication", 0, REG_SZ, reinterpret_cast<const BYTE*>(marker), 44) == ERROR_SUCCESS, "fixture setup failed");
        tank::StartupStore store(exe, key);
        Check(store.Read() == tank::StartupMode::Disabled, "new store should be disabled");
        Check(store.Set(tank::StartupMode::Windows) == ERROR_SUCCESS && store.Read() == tank::StartupMode::Windows, "cannot enable Windows startup");
        Check(store.Set(tank::StartupMode::FollowQQ) == ERROR_SUCCESS && store.Read() == tank::StartupMode::FollowQQ, "cannot switch to follow mode");
        Check(store.Set(tank::StartupMode::Disabled) == ERROR_SUCCESS && store.Read() == tank::StartupMode::Disabled, "cannot disable app startup");
        DWORD size = 0;
        Check(RegQueryValueExW(registry, L"OtherApplication", nullptr, nullptr, nullptr, &size) == ERROR_SUCCESS, "startup setting touched another application's value");
        const wchar_t* foreign = L"\"C:\\Other.exe\" --tray";
        RegSetValueExW(registry, L"TankStormKeepAlive", 0, REG_SZ, reinterpret_cast<const BYTE*>(foreign), static_cast<DWORD>((wcslen(foreign) + 1) * sizeof(wchar_t)));
        Check(store.Set(tank::StartupMode::Windows) == ERROR_ALREADY_EXISTS, "must not overwrite an unrelated command occupying the same name");
        RegCloseKey(registry); registry = nullptr;
        RegDeleteKeyW(HKEY_CURRENT_USER, key.c_str());
        std::cout << "PASS: startup quoting, opt-in modes, private registry round-trip, unrelated-value preservation\n";
    } catch (const std::exception& error) {
        if (registry) RegCloseKey(registry);
        if (disposition == REG_CREATED_NEW_KEY) RegDeleteKeyW(HKEY_CURRENT_USER, key.c_str());
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
