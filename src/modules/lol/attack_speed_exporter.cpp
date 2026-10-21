#include "attack_speed_exporter.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <locale>
#include <sstream>
#include <utility>

namespace {

constexpr wchar_t kOutputPath[] = L"D:\\LOL.txt";
constexpr wchar_t kTemporaryOutputPath[] = L"D:\\LOL.txt.tmp";
constexpr wchar_t kActivePlayerPath[] = L"/liveclientdata/activeplayer";
constexpr unsigned long kWaitingPollMilliseconds = 1000;
constexpr int kRequestTimeoutMilliseconds = 900;
constexpr std::size_t kMaximumResponseBytes = 1024 * 1024;

class InternetHandle final {
public:
    explicit InternetHandle(HINTERNET handle = nullptr) : handle_(handle) {}
    ~InternetHandle() {
        if (handle_) WinHttpCloseHandle(handle_);
    }

    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;

    HINTERNET Get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

private:
    HINTERNET handle_;
};

std::size_t FindObjectEnd(const std::string& json, std::size_t openBrace) {
    int depth = 0;
    bool inString = false;
    bool escaped = false;

    for (std::size_t index = openBrace; index < json.size(); ++index) {
        const char current = json[index];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (current == '\\') {
                escaped = true;
            } else if (current == '"') {
                inString = false;
            }
            continue;
        }

        if (current == '"') {
            inString = true;
        } else if (current == '{') {
            ++depth;
        } else if (current == '}') {
            --depth;
            if (depth == 0) return index;
        }
    }

    return std::string::npos;
}

bool FetchAttackSpeed(HINTERNET connection, double& attackSpeed) {
    InternetHandle request(WinHttpOpenRequest(connection, L"GET", kActivePlayerPath, nullptr,
                                               WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               WINHTTP_FLAG_SECURE));
    if (!request) return false;

    DWORD securityFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                          SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    if (!WinHttpSetOption(request.Get(), WINHTTP_OPTION_SECURITY_FLAGS,
                          &securityFlags, sizeof(securityFlags))) {
        return false;
    }

    if (!WinHttpSendRequest(request.Get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.Get(), nullptr)) {
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusBytes = sizeof(statusCode);
    if (!WinHttpQueryHeaders(request.Get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusBytes,
                             WINHTTP_NO_HEADER_INDEX) ||
        statusCode != 200) {
        return false;
    }

    std::string body;
    while (body.size() < kMaximumResponseBytes) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.Get(), &available)) return false;
        if (available == 0) break;

        const DWORD allowed = static_cast<DWORD>(
            std::min<std::size_t>(available, kMaximumResponseBytes - body.size()));
        std::string chunk(allowed, '\0');
        DWORD bytesRead = 0;
        if (!WinHttpReadData(request.Get(), chunk.data(), allowed, &bytesRead)) return false;
        chunk.resize(bytesRead);
        body += chunk;
        if (allowed < available) return false;
    }

    return AttackSpeedProtocol::TryParseAttackSpeedJson(body, attackSpeed);
}

bool SameState(const AttackSpeedState& left, const AttackSpeedState& right) {
    return left.detected == right.detected &&
           left.outputWritten == right.outputWritten &&
           (!left.detected || left.attackSpeed == right.attackSpeed);
}

}  // namespace

namespace AttackSpeedProtocol {

const wchar_t* OutputPath() {
    return kOutputPath;
}

bool TryParseAttackSpeedJson(const std::string& json, double& attackSpeed) {
    constexpr char championStatsKey[] = "\"championStats\"";
    constexpr char attackSpeedKey[] = "\"attackSpeed\"";

    const std::size_t championStats = json.find(championStatsKey);
    if (championStats == std::string::npos) return false;

    const std::size_t objectStart = json.find('{', championStats + sizeof(championStatsKey) - 1);
    if (objectStart == std::string::npos) return false;
    const std::size_t objectEnd = FindObjectEnd(json, objectStart);
    if (objectEnd == std::string::npos) return false;

    const std::size_t key = json.find(attackSpeedKey, objectStart + 1);
    if (key == std::string::npos || key >= objectEnd) return false;
    const std::size_t colon = json.find(':', key + sizeof(attackSpeedKey) - 1);
    if (colon == std::string::npos || colon >= objectEnd) return false;

    const char* numberStart = json.c_str() + colon + 1;
    char* numberEnd = nullptr;
    const double parsed = std::strtod(numberStart, &numberEnd);
    if (numberEnd == numberStart || numberEnd > json.c_str() + objectEnd ||
        !std::isfinite(parsed) || parsed <= 0.0) {
        return false;
    }

    attackSpeed = parsed;
    return true;
}

std::string FormatWaitingLua() {
    return "return {\r\n   status = \"WAITING\"\r\n}";
}

std::string FormatAttackSpeedLua(double attackSpeed) {
    if (!std::isfinite(attackSpeed) || attackSpeed <= 0.0) return FormatWaitingLua();

    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "return {\r\n   status = \"OK\",\r\n   attackSpeed = "
           << std::fixed << std::setprecision(2) << attackSpeed << "\r\n}";
    return output.str();
}

}  // namespace AttackSpeedProtocol

AttackSpeedExporter::~AttackSpeedExporter() {
    Stop();
}

void AttackSpeedExporter::Start(StateCallback callback) {
    if (worker_.joinable()) return;
    callback_ = std::move(callback);
    stopRequested_ = false;
    worker_ = std::thread(&AttackSpeedExporter::Run, this);
}

void AttackSpeedExporter::Stop() {
    stopRequested_ = true;
    waitCondition_.notify_all();
    if (worker_.joinable()) worker_.join();
    callback_ = nullptr;
}

void AttackSpeedExporter::SetConnectedPollInterval(unsigned long milliseconds) {
    const unsigned long normalized = std::clamp(milliseconds,
                                                MinimumPollIntervalMilliseconds,
                                                MaximumPollIntervalMilliseconds);
    if (connectedPollMilliseconds_.exchange(normalized) == normalized) return;
    configurationVersion_.fetch_add(1);
    waitCondition_.notify_all();
}

unsigned long AttackSpeedExporter::GetConnectedPollInterval() const {
    return connectedPollMilliseconds_.load();
}

void AttackSpeedExporter::Run() {
    bool hasPrevious = false;
    AttackSpeedState previous;

    const bool initialWrite = WriteOutputIfChanged(AttackSpeedProtocol::FormatWaitingLua());
    PublishIfChanged({false, initialWrite, 0.0}, hasPrevious, previous);

    InternetHandle session(WinHttpOpen(L"LOLAcceptLite-AttackSpeed/1.0",
                                       WINHTTP_ACCESS_TYPE_NO_PROXY,
                                       WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (session) {
        WinHttpSetTimeouts(session.Get(), kRequestTimeoutMilliseconds, kRequestTimeoutMilliseconds,
                           kRequestTimeoutMilliseconds, kRequestTimeoutMilliseconds);
    }
    InternetHandle connection(session ? WinHttpConnect(session.Get(), L"127.0.0.1", 2999, 0) : nullptr);

    while (!stopRequested_.load()) {
        double attackSpeed = 0.0;
        if (connection && FetchAttackSpeed(connection.Get(), attackSpeed)) {
            const std::string content = AttackSpeedProtocol::FormatAttackSpeedLua(attackSpeed);
            const bool written = WriteOutputIfChanged(content);
            const double displayedSpeed = std::round(attackSpeed * 100.0) / 100.0;
            PublishIfChanged({true, written, displayedSpeed}, hasPrevious, previous);
            if (WaitOrStop(connectedPollMilliseconds_.load())) break;
        } else {
            const bool written = WriteOutputIfChanged(AttackSpeedProtocol::FormatWaitingLua());
            PublishIfChanged({false, written, 0.0}, hasPrevious, previous);
            if (WaitOrStop(kWaitingPollMilliseconds)) break;
        }
    }
}

bool AttackSpeedExporter::WaitOrStop(unsigned long milliseconds) {
    const std::uint64_t version = configurationVersion_.load();
    std::unique_lock<std::mutex> lock(waitMutex_);
    waitCondition_.wait_for(lock, std::chrono::milliseconds(milliseconds), [this, version] {
        return stopRequested_.load() || configurationVersion_.load() != version;
    });
    return stopRequested_.load();
}

bool AttackSpeedExporter::WriteOutputIfChanged(const std::string& content) {
    if (lastWrittenContent_ == content && GetFileAttributesW(kOutputPath) != INVALID_FILE_ATTRIBUTES) {
        return true;
    }

    const std::string bytes = content + "\r\n";
    for (int attempt = 0; attempt < 40 && !stopRequested_.load(); ++attempt) {
        HANDLE file = CreateFileW(kTemporaryOutputPath, GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
        bool written = false;
        if (file != INVALID_HANDLE_VALUE) {
            DWORD totalWritten = 0;
            while (totalWritten < bytes.size()) {
                DWORD partWritten = 0;
                const DWORD remaining = static_cast<DWORD>(bytes.size() - totalWritten);
                if (!WriteFile(file, bytes.data() + totalWritten, remaining, &partWritten, nullptr) ||
                    partWritten == 0) {
                    break;
                }
                totalWritten += partWritten;
            }
            written = totalWritten == bytes.size() && FlushFileBuffers(file);
            CloseHandle(file);
        }

        if (written && MoveFileExW(kTemporaryOutputPath, kOutputPath,
                                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            lastWrittenContent_ = content;
            return true;
        }

        DeleteFileW(kTemporaryOutputPath);
        if (WaitOrStop(25)) break;
    }

    return false;
}

void AttackSpeedExporter::PublishIfChanged(const AttackSpeedState& state,
                                            bool& hasPrevious,
                                            AttackSpeedState& previous) {
    if (hasPrevious && SameState(state, previous)) return;
    previous = state;
    hasPrevious = true;
    if (callback_) callback_(state);
}
