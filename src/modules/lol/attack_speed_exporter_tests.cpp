#include "attack_speed_exporter.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>

namespace {

bool Check(bool condition, const char* message) {
    if (condition) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

}  // namespace

int main() {
    bool passed = true;
    double attackSpeed = 0.0;

    passed &= Check(AttackSpeedProtocol::TryParseAttackSpeedJson(
                        R"({"attackSpeed":9,"championStats":{"armor":20,"attackSpeed":2.834}})",
                        attackSpeed),
                    "valid championStats.attackSpeed should parse");
    passed &= Check(std::abs(attackSpeed - 2.834) < 0.000001,
                    "parsed attack speed should retain precision");
    passed &= Check(!AttackSpeedProtocol::TryParseAttackSpeedJson(
                        R"({"championStats":{"attackSpeed":0}})", attackSpeed),
                    "zero attack speed should be rejected");
    passed &= Check(!AttackSpeedProtocol::TryParseAttackSpeedJson(
                        R"({"attackSpeed":2.5,"championStats":{"armor":20}})", attackSpeed),
                    "attack speed outside championStats should be rejected");
    passed &= Check(AttackSpeedProtocol::FormatAttackSpeedLua(2.834) ==
                        "return {\r\n   status = \"OK\",\r\n   attackSpeed = 2.83\r\n}",
                    "OK output should match the Lua contract");
    passed &= Check(AttackSpeedProtocol::FormatAttackSpeedLua(
                        std::numeric_limits<double>::quiet_NaN()) ==
                        AttackSpeedProtocol::FormatWaitingLua(),
                    "invalid speed should produce WAITING");

    AttackSpeedExporter exporter;
    passed &= Check(exporter.GetConnectedPollInterval() == 100,
                    "default connected interval should be 100 ms");
    exporter.SetConnectedPollInterval(250);
    passed &= Check(exporter.GetConnectedPollInterval() == 250,
                    "custom connected interval should be retained");
    exporter.SetConnectedPollInterval(1);
    passed &= Check(exporter.GetConnectedPollInterval() == 20,
                    "connected interval should clamp to the 20 ms minimum");
    exporter.SetConnectedPollInterval(99999);
    passed &= Check(exporter.GetConnectedPollInterval() == 5000,
                    "connected interval should clamp to the 5000 ms maximum");

    return passed ? 0 : 1;
}
