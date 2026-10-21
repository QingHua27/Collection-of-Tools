#include "gateway.h"
static BOOL KeepAliveMessage(LPMSG, HWND, UINT, UINT);
#define wWinMain KeepAliveEntry
#define GetMessageW KeepAliveMessage
#define Shell_NotifyIconW toolbox::NotifyIcon
#include "modules/keepalive/main.cpp"
#undef Shell_NotifyIconW
#undef GetMessageW
#undef wWinMain

static void KeepAliveAction(toolbox::Action action) {
    using namespace toolbox;
    if (action == Primary) TogglePause();
    if (action == Secondary) Cycle(!paused);
    if (action == Refresh || action == Primary || action == Secondary) {
        std::wstring text = paused ? L"已暂停" : L"后台保活已开启";
        text += L" · " + std::to_wstring(targets.size()) + L" 个游戏页面\r\n每 30 秒发送后台移动消息，不移动真实鼠标。";
        text += L"\r\n\r\n最近发送：" + lastPulse;
        unsigned index = 0;
        for (auto& target : targets) {
            auto& count = counts[target.browser];
            text += L"\r\n页面 " + std::to_wstring(++index) + L"：已发送 " + std::to_wstring(count.queued) + L"，失败 " + std::to_wstring(count.failed);
        }
        text += L"\r\n\r\n消息成功入队不代表游戏在线；实际效果请在游戏内确认。";
        StateText(KeepAlive, text);
    }
    if (action == Stop && IsWindow(window)) DestroyWindow(window);
}
static BOOL KeepAliveMessage(LPMSG msg, HWND h, UINT low, UINT high) {
    if (toolbox::slots[toolbox::KeepAlive].window.load() != window) {
        toolbox::Publish(toolbox::KeepAlive, window); KeepAliveAction(toolbox::Refresh);
    }
    return toolbox::NextMessage(msg, h, low, high, KeepAliveAction);
}
void toolbox::RunKeepAlive() {
    running = true; paused = false; counts.clear();
    watchQQ = false; qqPresent = true;
    WritePrivateProfileStringW(L"modules",L"keepalive_follow_qq",nullptr,
        (DataDir()+L"\\toolbox.ini").c_str());
    KeepAliveEntry(GetModuleHandleW(nullptr), nullptr, nullptr, SW_HIDE);
    UnregisterClassW(kWindowClass, GetModuleHandleW(nullptr));
}
