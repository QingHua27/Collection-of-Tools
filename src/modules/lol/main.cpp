#include <windows.h>
#include <winhttp.h>
#include <winternl.h>
#include <tlhelp32.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "attack_speed_exporter.h"
#include "resource.h"

namespace fs = std::filesystem;

constexpr UINT WM_TRAYICON = WM_APP + 1;
constexpr UINT WM_REFRESH_UI = WM_APP + 2;
constexpr UINT ID_TRAY_AUTO_ACCEPT = 5001;
constexpr UINT ID_TRAY_MANUAL_ACCEPT = 5002;
constexpr UINT ID_TRAY_OPEN_OUTPUT = 5003;
constexpr UINT ID_TRAY_STARTUP = 5004;
constexpr UINT ID_TRAY_EXIT = 5005;
constexpr UINT ID_TRAY_POLL_INTERVAL = 5006;
constexpr UINT ID_POLL_TIMER = 7001;
constexpr wchar_t STARTUP_VALUE_NAME[] = L"LOLAcceptLite";
constexpr wchar_t SETTINGS_KEY_PATH[] = L"Software\\CppToolbox\\League";
constexpr wchar_t POLL_INTERVAL_VALUE_NAME[] = L"AttackSpeedPollIntervalMs";

struct LcuCredentials {
    int port = 0;
    std::wstring password;
    std::wstring protocol = L"https";
    fs::path source;
};

struct AppState {
    HWND hwnd = nullptr;
    NOTIFYICONDATAW nid{};
    std::atomic<bool> autoAccept{true};
    std::atomic<bool> startWithWindows{false};
    std::atomic<bool> exiting{false};
    std::atomic<bool> pollRunning{false};
    std::atomic<bool> websocketRunning{false};
    std::atomic<bool> websocketConnected{false};
    std::atomic<bool> accepting{false};
    std::atomic<HINTERNET> websocket{nullptr};
    std::mutex stateMutex;
    bool connected = false;
    int acceptedCount = 0;
    bool attackSpeedDetected = false;
    bool attackSpeedOutputWritten = true;
    double attackSpeed = 0.0;
    std::wstring status = L"等待英雄联盟客户端启动";
    std::wstring detail = L"自动接受已开启。";
    LcuCredentials credentials;
};

AppState g_app;
AttackSpeedExporter g_attackSpeedExporter;
std::atomic<unsigned> g_manualWorkers{0};

std::wstring GetExecutablePath() {
    std::wstring path(MAX_PATH, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    while (length == path.size()) {
        path.resize(path.size() * 2);
        length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    }
    path.resize(length);
    return path;
}

std::wstring QuoteArgument(const std::wstring& value) {
    return L"\"" + value + L"\"";
}

bool ReadStartupValue(std::wstring* value = nullptr) {
    HKEY key = nullptr;
    LONG opened = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                0, KEY_QUERY_VALUE, &key);
    if (opened != ERROR_SUCCESS) return false;

    DWORD type = 0;
    DWORD bytes = 0;
    LONG queried = RegQueryValueExW(key, STARTUP_VALUE_NAME, nullptr, &type, nullptr, &bytes);
    if (queried != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || bytes == 0) {
        RegCloseKey(key);
        return false;
    }

    std::wstring data(bytes / sizeof(wchar_t), L'\0');
    queried = RegQueryValueExW(key, STARTUP_VALUE_NAME, nullptr, &type,
                               reinterpret_cast<LPBYTE>(data.data()), &bytes);
    RegCloseKey(key);
    if (queried != ERROR_SUCCESS) return false;

    while (!data.empty() && data.back() == L'\0') data.pop_back();
    if (value) *value = data;
    return true;
}

bool IsStartupEnabled() {
    return ReadStartupValue();
}

bool SetStartupEnabled(bool enabled) {
    HKEY key = nullptr;
    LONG created = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                   0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (created != ERROR_SUCCESS) return false;

    LONG result = ERROR_SUCCESS;
    if (enabled) {
        std::wstring command = QuoteArgument(GetExecutablePath()) + L" --tray";
        result = RegSetValueExW(key, STARTUP_VALUE_NAME, 0, REG_SZ,
                                reinterpret_cast<const BYTE*>(command.c_str()),
                                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        result = RegDeleteValueW(key, STARTUP_VALUE_NAME);
        if (result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS;
    }

    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

void SyncStartupState() {
    g_app.startWithWindows = IsStartupEnabled();
}

unsigned long ReadAttackSpeedPollInterval() {
    DWORD value = 0;
    DWORD bytes = sizeof(value);
    const LONG result = RegGetValueW(HKEY_CURRENT_USER, SETTINGS_KEY_PATH,
                                     POLL_INTERVAL_VALUE_NAME, RRF_RT_REG_DWORD,
                                     nullptr, &value, &bytes);
    if (result != ERROR_SUCCESS ||
        value < AttackSpeedExporter::MinimumPollIntervalMilliseconds ||
        value > AttackSpeedExporter::MaximumPollIntervalMilliseconds) {
        return AttackSpeedExporter::DefaultPollIntervalMilliseconds;
    }
    return value;
}

bool WriteAttackSpeedPollInterval(unsigned long milliseconds) {
    HKEY key = nullptr;
    const LONG created = RegCreateKeyExW(HKEY_CURRENT_USER, SETTINGS_KEY_PATH,
                                         0, nullptr, 0, KEY_SET_VALUE,
                                         nullptr, &key, nullptr);
    if (created != ERROR_SUCCESS) return false;

    const DWORD value = milliseconds;
    const LONG written = RegSetValueExW(key, POLL_INTERVAL_VALUE_NAME, 0, REG_DWORD,
                                        reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
    return written == ERROR_SUCCESS;
}

std::wstring Utf8ToWide(const std::string& input) {
    if (input.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);
    std::wstring output(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), output.data(), len);
    return output;
}

std::string WideToUtf8(const std::wstring& input) {
    if (input.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
    std::string output(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), output.data(), len, nullptr, nullptr);
    return output;
}

std::vector<std::wstring> SplitWide(const std::wstring& text, wchar_t delimiter) {
    std::vector<std::wstring> parts;
    std::wstringstream stream(text);
    std::wstring item;
    while (std::getline(stream, item, delimiter)) {
        parts.push_back(item);
    }
    return parts;
}

std::wstring ReadTextFileUtf8(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return L"";
    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return Utf8ToWide(bytes);
}

std::wstring ReadProcessCommandLine(DWORD processId) {
    using NtQueryInformationProcessFn = NTSTATUS(NTAPI*)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);

    auto ntQueryInformationProcess = reinterpret_cast<NtQueryInformationProcessFn>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
    if (!ntQueryInformationProcess) return L"";

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, processId);
    if (!process) return L"";

    PROCESS_BASIC_INFORMATION pbi{};
    NTSTATUS status = ntQueryInformationProcess(process, ProcessBasicInformation, &pbi, sizeof(pbi), nullptr);
    if (status < 0 || !pbi.PebBaseAddress) {
        CloseHandle(process);
        return L"";
    }

    PEB peb{};
    SIZE_T bytesRead = 0;
    if (!ReadProcessMemory(process, pbi.PebBaseAddress, &peb, sizeof(peb), &bytesRead) || !peb.ProcessParameters) {
        CloseHandle(process);
        return L"";
    }

    RTL_USER_PROCESS_PARAMETERS params{};
    if (!ReadProcessMemory(process, peb.ProcessParameters, &params, sizeof(params), &bytesRead) ||
        !params.CommandLine.Buffer || params.CommandLine.Length == 0) {
        CloseHandle(process);
        return L"";
    }

    std::wstring commandLine(params.CommandLine.Length / sizeof(wchar_t), L'\0');
    if (!ReadProcessMemory(process, params.CommandLine.Buffer, commandLine.data(), params.CommandLine.Length, &bytesRead)) {
        CloseHandle(process);
        return L"";
    }

    CloseHandle(process);
    return commandLine;
}

std::wstring ExtractCommandLineValue(const std::wstring& commandLine, const std::wstring& name) {
    std::wstring needle = L"--" + name + L"=";
    size_t pos = commandLine.find(needle);
    if (pos == std::wstring::npos) return L"";

    pos += needle.size();
    size_t end = pos;
    while (end < commandLine.size() && commandLine[end] != L'"' && commandLine[end] != L' ' && commandLine[end] != L'\t') {
        ++end;
    }
    return commandLine.substr(pos, end - pos);
}

bool ParseCredentialsFromCommandLine(const std::wstring& commandLine, LcuCredentials& credentials) {
    std::wstring port = ExtractCommandLineValue(commandLine, L"app-port");
    std::wstring token = ExtractCommandLineValue(commandLine, L"remoting-auth-token");
    if (port.empty() || token.empty()) return false;

    try {
        credentials.port = std::stoi(port);
    } catch (...) {
        return false;
    }

    credentials.password = token;
    credentials.protocol = L"https";
    credentials.source.clear();
    return credentials.port > 0 && !credentials.password.empty();
}

std::wstring Base64Encode(const std::string& input) {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0;
    int valb = -6;
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(alphabet[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(alphabet[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return Utf8ToWide(out);
}

bool ParseLockfile(const fs::path& path, LcuCredentials& credentials) {
    std::wstring content = ReadTextFileUtf8(path);
    if (content.empty()) return false;
    while (!content.empty() && (content.back() == L'\r' || content.back() == L'\n' || content.back() == L'\0')) {
        content.pop_back();
    }

    auto parts = SplitWide(content, L':');
    if (parts.size() < 5) return false;

    try {
        credentials.port = std::stoi(parts[2]);
    } catch (...) {
        return false;
    }
    credentials.password = parts[3];
    credentials.protocol = parts[4].empty() ? L"https" : parts[4];
    credentials.source = path;
    return credentials.port > 0 && !credentials.password.empty();
}

std::vector<fs::path> CandidateLockfilesFromProcesses() {
    std::vector<fs::path> result;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return result;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            std::wstring exe = entry.szExeFile;
            for (auto& ch : exe) ch = static_cast<wchar_t>(std::towlower(ch));
            if (exe != L"leagueclientux.exe" && exe != L"leagueclient.exe") continue;

            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
            if (!process) continue;
            wchar_t pathBuffer[MAX_PATH * 4]{};
            DWORD size = static_cast<DWORD>(std::size(pathBuffer));
            if (QueryFullProcessImageNameW(process, 0, pathBuffer, &size)) {
                fs::path exePath(pathBuffer);
                result.push_back(exePath.parent_path() / L"lockfile");
            }
            CloseHandle(process);
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return result;
}

std::vector<fs::path> CandidateLockfiles() {
    std::vector<fs::path> result = CandidateLockfilesFromProcesses();
    result.push_back(L"C:\\Riot Games\\League of Legends\\lockfile");
    result.push_back(L"C:\\Program Files\\Riot Games\\League of Legends\\lockfile");
    result.push_back(L"C:\\Program Files (x86)\\Riot Games\\League of Legends\\lockfile");

    wchar_t localAppData[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) > 0) {
        result.push_back(fs::path(localAppData) / L"Riot Games" / L"League of Legends" / L"lockfile");
    }
    return result;
}

bool DiscoverLcu(LcuCredentials& credentials) {
    for (const auto& path : CandidateLockfiles()) {
        std::error_code ec;
        if (!fs::exists(path, ec)) continue;
        if (ParseLockfile(path, credentials)) return true;
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            std::wstring exe = entry.szExeFile;
            for (auto& ch : exe) ch = static_cast<wchar_t>(std::towlower(ch));
            if (exe != L"leagueclientux.exe" && exe != L"leagueclient.exe") continue;

            std::wstring commandLine = ReadProcessCommandLine(entry.th32ProcessID);
            if (ParseCredentialsFromCommandLine(commandLine, credentials)) {
                CloseHandle(snapshot);
                return true;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return false;
}

struct HttpResult {
    DWORD statusCode = 0;
    std::string body;
    std::wstring error;
};

HttpResult LcuRequest(const LcuCredentials& credentials, const std::wstring& method, const std::wstring& path) {
    HttpResult result;
    HINTERNET session = WinHttpOpen(L"LOLAcceptLite/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        result.error = L"WinHTTP 初始化失败";
        return result;
    }

    HINTERNET connect = WinHttpConnect(session, L"127.0.0.1", static_cast<INTERNET_PORT>(credentials.port), 0);
    if (!connect) {
        result.error = L"无法连接到 LCU 端口";
        WinHttpCloseHandle(session);
        return result;
    }

    HINTERNET request = WinHttpOpenRequest(connect, method.c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request) {
        result.error = L"无法创建 HTTP 请求";
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return result;
    }

    DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                  SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof(flags));
    WinHttpSetTimeouts(request, 3000, 3000, 3000, 3000);

    std::wstring auth = L"Authorization: Basic " + Base64Encode("riot:" + WideToUtf8(credentials.password)) + L"\r\n";
    std::wstring headers = auth + L"Accept: application/json\r\nContent-Length: 0\r\n";

    BOOL ok = WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(headers.size()),
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (ok) ok = WinHttpReceiveResponse(request, nullptr);
    if (!ok) {
        result.error = L"LCU 请求失败";
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return result;
    }

    DWORD statusSize = sizeof(result.statusCode);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &result.statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

    DWORD available = 0;
    do {
        available = 0;
        if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, chunk.data(), available, &read)) break;
        chunk.resize(read);
        result.body += chunk;
    } while (available > 0);

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return result;
}

bool BodyLooksLikeReadyCheck(const std::string& body) {
    const bool inProgress = body.find("\"state\":\"InProgress\"") != std::string::npos ||
                            body.find("\"state\": \"InProgress\"") != std::string::npos;
    const bool unanswered = body.find("\"playerResponse\":\"None\"") != std::string::npos ||
                            body.find("\"playerResponse\": \"None\"") != std::string::npos;
    return inProgress && unanswered;
}

bool IsReadyCheckEvent(const std::string& message) {
    return message.find("/lol-matchmaking/v1/ready-check") != std::string::npos &&
           BodyLooksLikeReadyCheck(message);
}

void SetStatus(std::wstring status, std::wstring detail, bool connected, const LcuCredentials* credentials = nullptr) {
    {
        std::lock_guard<std::mutex> lock(g_app.stateMutex);
        g_app.status = std::move(status);
        g_app.detail = std::move(detail);
        g_app.connected = connected;
        if (credentials) g_app.credentials = *credentials;
    }
    if (!g_app.exiting.load() && g_app.hwnd) PostMessageW(g_app.hwnd, WM_REFRESH_UI, 0, 0);
}

void SetAttackSpeedState(const AttackSpeedState& state) {
    {
        std::lock_guard<std::mutex> lock(g_app.stateMutex);
        g_app.attackSpeedDetected = state.detected;
        g_app.attackSpeedOutputWritten = state.outputWritten;
        g_app.attackSpeed = state.attackSpeed;
    }
    if (!g_app.exiting.load() && g_app.hwnd) PostMessageW(g_app.hwnd, WM_REFRESH_UI, 0, 0);
}

std::wstring AttackSpeedStatusText() {
    if (!g_app.attackSpeedOutputWritten) return L"攻速导出：写入 D:\\LOL.txt 失败";
    if (!g_app.attackSpeedDetected) return L"攻速导出：等待进入游戏";

    wchar_t text[80]{};
    swprintf_s(text, L"攻速导出：%.2f", g_app.attackSpeed);
    return text;
}

void RefreshUi() {
    std::lock_guard<std::mutex> lock(g_app.stateMutex);

    if (g_app.nid.cbSize) {
        std::wstring tip = L"LOL 工具 - " + g_app.status;
        if (g_app.attackSpeedDetected) {
            wchar_t speed[32]{};
            swprintf_s(speed, L" | AS %.2f", g_app.attackSpeed);
            tip += speed;
        }
        wcsncpy_s(g_app.nid.szTip, tip.c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &g_app.nid);
    }
}

bool AcceptMatch(const LcuCredentials& credentials, std::wstring& message) {
    HttpResult result = LcuRequest(credentials, L"POST", L"/lol-matchmaking/v1/ready-check/accept");
    if (result.statusCode >= 200 && result.statusCode < 300) {
        message = L"已发送接受对战请求";
        return true;
    }
    message = result.error.empty() ? (L"接受失败，HTTP " + std::to_wstring(result.statusCode)) : result.error;
    return false;
}

void AcceptAutomatically(const LcuCredentials& credentials) {
    if (!g_app.autoAccept.load() || g_app.accepting.exchange(true)) return;

    std::wstring message;
    bool accepted = AcceptMatch(credentials, message);
    if (accepted) {
        {
            std::lock_guard<std::mutex> lock(g_app.stateMutex);
            ++g_app.acceptedCount;
        }
        SetStatus(L"已自动接受对战", message, true, &credentials);
    } else {
        SetStatus(L"自动接受失败", message, true, &credentials);
    }
    g_app.accepting = false;
}

void SleepWhileRunning(DWORD milliseconds) {
    constexpr DWORD step = 100;
    for (DWORD elapsed = 0; elapsed < milliseconds && !g_app.exiting.load(); elapsed += step) {
        Sleep(std::min(step, milliseconds - elapsed));
    }
}

void RunWebSocket(const LcuCredentials& credentials) {
    HINTERNET session = WinHttpOpen(L"LOLAcceptLite/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return;

    HINTERNET connect = WinHttpConnect(session, L"127.0.0.1",
                                       static_cast<INTERNET_PORT>(credentials.port), 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        return;
    }

    HINTERNET request = WinHttpOpenRequest(connect, L"GET", L"/", nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return;
    }

    DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                  SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof(flags));
    WinHttpSetTimeouts(request, 3000, 3000, 3000, 3000);
    WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0);

    std::wstring auth = L"Authorization: Basic " + Base64Encode("riot:" + WideToUtf8(credentials.password)) + L"\r\n";
    BOOL ok = WinHttpSendRequest(request, auth.c_str(), static_cast<DWORD>(auth.size()),
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (ok) ok = WinHttpReceiveResponse(request, nullptr);

    HINTERNET websocket = ok ? WinHttpWebSocketCompleteUpgrade(request, 0) : nullptr;
    WinHttpCloseHandle(request);
    if (!websocket) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return;
    }

    g_app.websocket = websocket;
    const std::string subscription = "[5,\"OnJsonApiEvent\"]";
    DWORD sent = WinHttpWebSocketSend(websocket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                      const_cast<char*>(subscription.data()),
                                      static_cast<DWORD>(subscription.size()));
    if (sent != NO_ERROR) {
        HINTERNET owned = g_app.websocket.exchange(nullptr);
        if (owned) WinHttpCloseHandle(owned);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return;
    }

    g_app.websocketConnected = true;
    SetStatus(L"已连接，等待匹配", L"已订阅客户端对局事件，检测到对局后会立即接受。", true, &credentials);

    // The first ready-check event can occur while the client connection is still being established.
    // Read the current state once after subscribing so that startup cannot miss that event.
    HttpResult initialReady = LcuRequest(credentials, L"GET", L"/lol-matchmaking/v1/ready-check");
    if (initialReady.statusCode >= 200 && initialReady.statusCode < 300 &&
        BodyLooksLikeReadyCheck(initialReady.body)) {
        if (g_app.autoAccept.load()) {
            AcceptAutomatically(credentials);
        } else {
            SetStatus(L"检测到对战确认", L"自动接受已关闭，可点击手动接受。", true, &credentials);
        }
    }

    std::string message;
    while (!g_app.exiting.load()) {
        char buffer[8192];
        DWORD bytesRead = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
        DWORD result = WinHttpWebSocketReceive(websocket, buffer, sizeof(buffer), &bytesRead, &type);
        if (result != NO_ERROR || type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) break;

        if (type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE ||
            type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
            message.append(buffer, bytesRead);
            if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
                if (IsReadyCheckEvent(message)) {
                    if (g_app.autoAccept.load()) {
                        AcceptAutomatically(credentials);
                    } else {
                        SetStatus(L"检测到对战确认", L"自动接受已关闭，可点击手动接受。", true, &credentials);
                    }
                }
                message.clear();
            }
        }
    }

    g_app.websocketConnected = false;
    HINTERNET owned = g_app.websocket.exchange(nullptr);
    if (owned) WinHttpCloseHandle(owned);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
}

void StartWebSocketWorker() {
    if (g_app.websocketRunning.exchange(true)) return;
    std::thread([] {
        while (!g_app.exiting.load()) {
            LcuCredentials credentials;
            if (!DiscoverLcu(credentials)) {
                SetStatus(L"等待英雄联盟客户端启动", L"未找到客户端，启动后将自动连接。", false);
                SleepWhileRunning(1000);
                continue;
            }

            RunWebSocket(credentials);
            if (!g_app.exiting.load()) {
                SetStatus(L"正在重新连接客户端", L"事件连接已断开，正在自动重连。", false, &credentials);
                SleepWhileRunning(1000);
            }
        }
        g_app.websocketConnected = false;
        g_app.websocketRunning = false;
    }).detach();
}

void PollOnce() {
    if (g_app.exiting.load()) return;
    if (g_app.websocketConnected.load()) return;
    if (g_app.pollRunning.exchange(true)) return;
    std::thread([] {
        LcuCredentials credentials;
        if (!DiscoverLcu(credentials)) {
            SetStatus(L"等待英雄联盟客户端启动", L"未找到 lockfile，请先启动英雄联盟客户端。", false);
            g_app.pollRunning = false;
            return;
        }

        HttpResult ready = LcuRequest(credentials, L"GET", L"/lol-matchmaking/v1/ready-check");
        if (ready.statusCode == 0) {
            SetStatus(L"LCU 连接中断", L"已找到客户端，但本地 API 暂时不可用。", false, &credentials);
            g_app.pollRunning = false;
            return;
        }

        if (ready.statusCode == 404) {
            SetStatus(L"已连接，等待匹配", L"当前没有对战确认。", true, &credentials);
            g_app.pollRunning = false;
            return;
        }

        if (ready.statusCode >= 200 && ready.statusCode < 300 && BodyLooksLikeReadyCheck(ready.body)) {
            if (g_app.autoAccept.load()) {
                AcceptAutomatically(credentials);
            } else {
                SetStatus(L"检测到对战确认", L"自动接受已关闭，可点击手动接受。", true, &credentials);
            }
            g_app.pollRunning = false;
            return;
        }

        SetStatus(L"已连接，等待匹配", L"客户端在线，当前未处于对战确认状态。", true, &credentials);
        g_app.pollRunning = false;
    }).detach();
}

void ManualAccept() {
    if (g_app.exiting || g_manualWorkers.exchange(1)) return;
    std::thread([] {
        struct Done { ~Done() { g_manualWorkers = 0; } } done;
        LcuCredentials credentials;
        if (!DiscoverLcu(credentials)) {
            SetStatus(L"手动接受失败", L"未找到英雄联盟客户端。", false);
            return;
        }
        std::wstring message;
        bool accepted = AcceptMatch(credentials, message);
        if (accepted) {
            {
                std::lock_guard<std::mutex> lock(g_app.stateMutex);
                ++g_app.acceptedCount;
            }
            SetStatus(L"已手动接受对战", message, true, &credentials);
        } else {
            SetStatus(L"手动接受失败", message, true, &credentials);
        }
    }).detach();
}

void ToggleStartup(bool enabled) {
    bool ok = SetStartupEnabled(enabled);
    g_app.startWithWindows = ok ? enabled : IsStartupEnabled();

    bool connected = false;
    {
        std::lock_guard<std::mutex> lock(g_app.stateMutex);
        connected = g_app.connected;
    }

    if (ok) {
        SetStatus(enabled ? L"开机自启动已开启" : L"开机自启动已关闭",
                  enabled ? L"下次登录 Windows 后会自动启动本工具。" : L"已从 Windows 启动项中移除。",
                  connected);
    } else {
        SetStatus(L"开机自启动设置失败", L"无法写入当前用户启动项，请检查系统权限。", connected);
    }
}

void ShowTrayMenu(HWND hwnd) {
    std::wstring clientStatus;
    std::wstring attackStatus;
    int acceptedCount = 0;
    {
        std::lock_guard<std::mutex> lock(g_app.stateMutex);
        clientStatus = L"客户端：" + g_app.status;
        attackStatus = AttackSpeedStatusText();
        acceptedCount = g_app.acceptedCount;
    }
    clientStatus += L" · 已接受 " + std::to_wstring(acceptedCount) + L" 次";

    POINT point;
    GetCursorPos(&point);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, clientStatus.c_str());
    AppendMenuW(menu, MF_STRING | (g_app.autoAccept.load() ? MF_CHECKED : MF_UNCHECKED),
                ID_TRAY_AUTO_ACCEPT, L"自动接受");
    AppendMenuW(menu, MF_STRING, ID_TRAY_MANUAL_ACCEPT, L"手动接受当前对局");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, attackStatus.c_str());
    wchar_t intervalLabel[96]{};
    swprintf_s(intervalLabel, L"攻速刷新间隔：%lu 毫秒...",
               g_attackSpeedExporter.GetConnectedPollInterval());
    AppendMenuW(menu, MF_STRING, ID_TRAY_POLL_INTERVAL, intervalLabel);
    AppendMenuW(menu, MF_STRING, ID_TRAY_OPEN_OUTPUT, L"打开 D:\\LOL.txt");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (g_app.startWithWindows.load() ? MF_CHECKED : MF_UNCHECKED),
                ID_TRAY_STARTUP, L"开机自启动");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"退出");
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
}

void ShowTrayBalloon(const wchar_t* title, const wchar_t* message, DWORD icon) {
    if (!g_app.nid.cbSize) return;
    NOTIFYICONDATAW notification = g_app.nid;
    notification.uFlags = NIF_INFO;
    notification.dwInfoFlags = icon;
    wcsncpy_s(notification.szInfoTitle, title, _TRUNCATE);
    wcsncpy_s(notification.szInfo, message, _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &notification);
}

struct PollIntervalDialogData {
    unsigned long milliseconds = AttackSpeedExporter::DefaultPollIntervalMilliseconds;
};

INT_PTR CALLBACK PollIntervalDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INITDIALOG) {
        auto* data = reinterpret_cast<PollIntervalDialogData*>(lParam);
        SetWindowLongPtrW(dialog, DWLP_USER, lParam);
        SetDlgItemInt(dialog, IDC_POLL_INTERVAL_EDIT, data->milliseconds, FALSE);
        HWND edit = GetDlgItem(dialog, IDC_POLL_INTERVAL_EDIT);
        SendMessageW(edit, EM_SETLIMITTEXT, 4, 0);
        SendMessageW(edit, EM_SETSEL, 0, -1);
        return TRUE;
    }

    if (message != WM_COMMAND) return FALSE;

    if (LOWORD(wParam) == IDOK) {
        BOOL translated = FALSE;
        const UINT value = GetDlgItemInt(dialog, IDC_POLL_INTERVAL_EDIT, &translated, FALSE);
        if (!translated ||
            value < AttackSpeedExporter::MinimumPollIntervalMilliseconds ||
            value > AttackSpeedExporter::MaximumPollIntervalMilliseconds) {
            MessageBoxW(dialog, L"请输入 20 到 5000 之间的整数。",
                        L"攻速刷新间隔", MB_OK | MB_ICONWARNING);
            return TRUE;
        }

        auto* data = reinterpret_cast<PollIntervalDialogData*>(
            GetWindowLongPtrW(dialog, DWLP_USER));
        data->milliseconds = value;
        EndDialog(dialog, IDOK);
        return TRUE;
    }

    if (LOWORD(wParam) == IDCANCEL) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }

    return FALSE;
}

void ConfigureAttackSpeedPollInterval() {
    PollIntervalDialogData data{g_attackSpeedExporter.GetConnectedPollInterval()};
    const INT_PTR result = DialogBoxParamW(GetModuleHandleW(nullptr),
                                           MAKEINTRESOURCEW(IDD_POLL_INTERVAL),
                                           nullptr, PollIntervalDialogProc,
                                           reinterpret_cast<LPARAM>(&data));
    if (result == IDCANCEL) return;
    if (result != IDOK) {
        ShowTrayBalloon(L"无法打开设置", L"攻速刷新间隔输入框创建失败。", NIIF_WARNING);
        return;
    }

    g_attackSpeedExporter.SetConnectedPollInterval(data.milliseconds);
    wchar_t message[128]{};
    if (WriteAttackSpeedPollInterval(data.milliseconds)) {
        swprintf_s(message, L"已设为 %lu 毫秒，并将在重启后保留。", data.milliseconds);
        ShowTrayBalloon(L"攻速刷新间隔已更新", message, NIIF_INFO);
    } else {
        swprintf_s(message, L"当前已设为 %lu 毫秒，但无法保存到用户配置。", data.milliseconds);
        ShowTrayBalloon(L"攻速刷新间隔已更新", message, NIIF_WARNING);
    }
}

void OpenAttackSpeedOutput(HWND hwnd) {
    if (GetFileAttributesW(AttackSpeedProtocol::OutputPath()) == INVALID_FILE_ATTRIBUTES) {
        ShowTrayBalloon(L"文件尚未创建", AttackSpeedProtocol::OutputPath(), NIIF_INFO);
        return;
    }

    const std::wstring arguments = L"\"" + std::wstring(AttackSpeedProtocol::OutputPath()) + L"\"";
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(hwnd, L"open", L"notepad.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        ShowTrayBalloon(L"无法打开文件", AttackSpeedProtocol::OutputPath(), NIIF_WARNING);
    }
}

void InitTray(HWND hwnd) {
    g_app.nid.cbSize = sizeof(g_app.nid);
    g_app.nid.hWnd = hwnd;
    g_app.nid.uID = 1;
    g_app.nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_app.nid.uCallbackMessage = WM_TRAYICON;
    g_app.nid.hIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON),
                                                     IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
    if (!g_app.nid.hIcon) g_app.nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_app.nid.szTip, L"LOL 自动接受 + 攻速导出");
    Shell_NotifyIconW(NIM_ADD, &g_app.nid);
    ShowTrayBalloon(L"LOL 工具已启动", L"自动接受和攻速导出正在托盘运行。", NIIF_INFO);
}

void CleanupTray() {
    if (g_app.nid.cbSize) {
        Shell_NotifyIconW(NIM_DELETE, &g_app.nid);
        if (g_app.nid.hIcon && g_app.nid.hIcon != LoadIconW(nullptr, IDI_APPLICATION)) {
            DestroyIcon(g_app.nid.hIcon);
        }
        g_app.nid = {};
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_app.hwnd = hwnd;
        SyncStartupState();
        g_attackSpeedExporter.SetConnectedPollInterval(ReadAttackSpeedPollInterval());
        InitTray(hwnd);
        RefreshUi();
        SetTimer(hwnd, ID_POLL_TIMER, 3000, nullptr);
        if (!toolbox::testMode) {
            StartWebSocketWorker();
            PollOnce();
            g_attackSpeedExporter.Start(SetAttackSpeedState);
        }
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TRAY_AUTO_ACCEPT: {
            const bool enabled = !g_app.autoAccept.load();
            g_app.autoAccept = enabled;
            WritePrivateProfileStringW(L"modules", L"lol_auto_accept", enabled ? L"1" : L"0",
                (toolbox::DataDir() + L"\\toolbox.ini").c_str());
            bool connected = false;
            {
                std::lock_guard<std::mutex> lock(g_app.stateMutex);
                connected = g_app.connected;
            }
            SetStatus(enabled ? L"自动接受已开启" : L"自动接受已关闭",
                      enabled ? L"检测到对战确认后会自动接受。" : L"只显示状态，不会自动接受。",
                      connected);
            return 0;
        }
        case ID_TRAY_STARTUP:
            ToggleStartup(!g_app.startWithWindows.load());
            return 0;
        case ID_TRAY_MANUAL_ACCEPT:
            ManualAccept();
            return 0;
        case ID_TRAY_OPEN_OUTPUT:
            OpenAttackSpeedOutput(hwnd);
            return 0;
        case ID_TRAY_POLL_INTERVAL:
            ConfigureAttackSpeedPollInterval();
            return 0;
        case ID_TRAY_EXIT:
            g_app.exiting = true;
            g_attackSpeedExporter.Stop();
            if (HINTERNET websocket = g_app.websocket.exchange(nullptr)) {
                WinHttpWebSocketClose(websocket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
                WinHttpCloseHandle(websocket);
            }
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_TIMER:
        if (wParam == ID_POLL_TIMER) {
            if (!toolbox::testMode) PollOnce();
            return 0;
        }
        break;
    case WM_TRAYICON:
        if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK || lParam == WM_RBUTTONUP) {
            ShowTrayMenu(hwnd);
        }
        return 0;
    case WM_REFRESH_UI:
        RefreshUi();
        return 0;
    case WM_DESTROY:
        g_app.exiting = true;
        KillTimer(hwnd, ID_POLL_TIMER);
        g_attackSpeedExporter.Stop();
        CleanupTray();
        g_app.hwnd = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, toolbox::testMode ? L"Local\\CppToolbox.League.Test" : L"Local\\CppToolbox.League.1");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return 0;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"LOLAcceptLiteMessageWindow";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"LOL 自动接受 + 攻速导出",
                                0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    if (!hwnd) return 1;

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (mutex) CloseHandle(mutex);
    return 0;
}
