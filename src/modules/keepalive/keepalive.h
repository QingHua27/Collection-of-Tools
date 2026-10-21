#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace tank {
struct Candidate {
    HWND root = nullptr;
    HWND browser = nullptr;
    HWND input = nullptr;
    DWORD pid = 0;
    std::wstring rootTitle;
    std::wstring rootPath;
    std::wstring inputPath;
    int width = 0;
    int height = 0;
    bool visible = false;
};
struct Delivery { bool queued = false; DWORD error = 0; };
std::vector<Candidate> SelectTargets(const std::vector<Candidate>& candidates);
std::vector<Candidate> DiscoverTargets();
Delivery PostMove(HWND input, bool alternate);
Delivery PulseTarget(const Candidate& target, bool alternate);
}
