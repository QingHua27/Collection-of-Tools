#include "keepalive.h"
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
HWND Handle(INT_PTR value) { return reinterpret_cast<HWND>(value); }
tank::Candidate Fixture(INT_PTR browser, INT_PTR input, bool visible) {
    return { Handle(1), Handle(browser), Handle(input), 22, L"坦克风暴",
        L"C:\\QQMicroGameBox\\4.1.4.4\\QQMicroGameBox.exe",
        L"C:\\QQMicroGameBox\\4.1.4.4\\CefBrowserProcess.exe", 960, 800, visible };
}
int moves = 0;
int clicks = 0;
POINT lastMove{};
std::vector<POINT> receivedMoves;
LRESULT CALLBACK TestProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_MOUSEMOVE) {
        ++moves;
        lastMove = { static_cast<short>(LOWORD(l)), static_cast<short>(HIWORD(l)) };
        receivedMoves.push_back(lastMove);
        Require(w == 0, "move must carry no pressed buttons or modifiers");
    }
    if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
        message == WM_RBUTTONDOWN || message == WM_RBUTTONUP) ++clicks;
    return DefWindowProcW(window, message, w, l);
}
void TargetTests() {
    const auto one = Fixture(10, 11, false);
    const auto two = Fixture(20, 21, true);
    auto targets = tank::SelectTargets({one, two, one});
    Require(targets.size() == 2, "must discover both accounts, including hidden page, without duplicates");
    Require(targets[0].input == Handle(11) && targets[1].input == Handle(21), "wrong account inputs selected");
    auto bad = one; bad.rootTitle = L"another game";
    Require(tank::SelectTargets({bad}).empty(), "wrong title accepted");
    bad = one; bad.rootPath = L"C:\\QQMicroGameBox\\4.1.4.4\\Other.exe";
    Require(tank::SelectTargets({bad}).empty(), "wrong host accepted");
    bad = one; bad.inputPath = L"C:\\Other\\CefBrowserProcess.exe";
    Require(tank::SelectTargets({bad}).empty(), "CEF outside the host directory accepted");
    bad = one; bad.inputPath = L"C:\\QQMicroGameBox\\4.1.4.4\\Other.exe";
    Require(tank::SelectTargets({bad}).empty(), "non-CEF process accepted");
    bad = one; bad.width = 1;
    Require(tank::SelectTargets({bad}).empty(), "undersized input accepted");
    bad = one; bad.input = nullptr;
    Require(tank::SelectTargets({bad}).empty(), "null input accepted");
    bad = one; bad.rootPath = L"c:\\qqmicrogamebox\\4.1.4.4\\QQMICROGAMEBOX.EXE";
    Require(tank::SelectTargets({bad}).size() == 1, "Windows path comparison must ignore case");
    std::vector<tank::Candidate> many;
    for (INT_PTR i = 0; i < 12; ++i) many.push_back(Fixture(100 + i * 2, 101 + i * 2, i % 3 == 0));
    Require(tank::SelectTargets(many).size() == 12, "must not impose a two-account limit");
}
void DeliveryTests() {
    WNDCLASSW cls{}; cls.lpfnWndProc = TestProc;
    cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = L"TankKeepAliveTestWindow";
    Require(RegisterClassW(&cls) != 0, "test window class failed");
    HWND window = CreateWindowW(cls.lpszClassName, L"unrelated fixture", WS_POPUP,
        0, 0, 100, 100, nullptr, nullptr, cls.hInstance, nullptr);
    Require(window != nullptr, "test window failed");
    POINT before{}; GetCursorPos(&before); HWND foreground = GetForegroundWindow();
    MSG message{};
    Require(tank::PostMove(window, false).queued, "first background move not queued");
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
    Require(tank::PostMove(window, true).queued, "second background move not queued");
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
    POINT after{}; GetCursorPos(&after);
    Require(moves == 2, "both scheduled positions must reach the hidden window");
    // The observed Flash GLOBAL idle guard requires abs(mouseX-lastX) > 5.
    Require(receivedMoves[1].x - receivedMoves[0].x > 5, "movement does not cross the actual Flash inactivity threshold");
    Require(receivedMoves[0].x == 34 && lastMove.x == 66 && lastMove.y == 50, "wrong centered positions");
    Require(clicks == 0, "unexpected click delivered");
    Require(before.x == after.x && before.y == after.y, "real pointer moved");
    Require(foreground == GetForegroundWindow(), "foreground window changed");
    auto fake = Fixture(10, 11, true); fake.input = window;
    Require(!tank::PulseTarget(fake, false).queued, "stale/unrelated target must fail closed");
    DestroyWindow(window);
    Require(!tank::PostMove(window, false).queued, "destroyed input must be rejected");
    Require(!tank::PostMove(nullptr, false).queued, "null input must be rejected");
    UnregisterClassW(cls.lpszClassName, cls.hInstance);
}
}
int main() {
    try { TargetTests(); DeliveryTests(); std::cout << "PASS: two-account routing, hidden pages, identity guards, Win32 delivery, no clicks/pointer/focus changes\n"; }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
