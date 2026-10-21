#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

struct AttackSpeedState {
    bool detected = false;
    bool outputWritten = true;
    double attackSpeed = 0.0;
};

namespace AttackSpeedProtocol {

const wchar_t* OutputPath();
bool TryParseAttackSpeedJson(const std::string& json, double& attackSpeed);
std::string FormatWaitingLua();
std::string FormatAttackSpeedLua(double attackSpeed);

}  // namespace AttackSpeedProtocol

class AttackSpeedExporter final {
public:
    using StateCallback = std::function<void(const AttackSpeedState&)>;

    AttackSpeedExporter() = default;
    ~AttackSpeedExporter();

    AttackSpeedExporter(const AttackSpeedExporter&) = delete;
    AttackSpeedExporter& operator=(const AttackSpeedExporter&) = delete;

    void Start(StateCallback callback);
    void Stop();
    void SetConnectedPollInterval(unsigned long milliseconds);
    unsigned long GetConnectedPollInterval() const;

    static constexpr unsigned long MinimumPollIntervalMilliseconds = 20;
    static constexpr unsigned long MaximumPollIntervalMilliseconds = 5000;
    static constexpr unsigned long DefaultPollIntervalMilliseconds = 100;

private:
    void Run();
    bool WaitOrStop(unsigned long milliseconds);
    bool WriteOutputIfChanged(const std::string& content);
    void PublishIfChanged(const AttackSpeedState& state, bool& hasPrevious, AttackSpeedState& previous);

    std::atomic<bool> stopRequested_{false};
    std::atomic<unsigned long> connectedPollMilliseconds_{DefaultPollIntervalMilliseconds};
    std::atomic<std::uint64_t> configurationVersion_{0};
    std::mutex waitMutex_;
    std::condition_variable waitCondition_;
    std::thread worker_;
    StateCallback callback_;
    std::string lastWrittenContent_;
};
