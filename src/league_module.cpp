#include "gateway.h"
#include "theme.h"
static BOOL LeagueMessage(LPMSG, HWND, UINT, UINT);
#define wWinMain LeagueEntry
#define WndProc LeagueWindowProc
#define GetMessageW LeagueMessage
#define Shell_NotifyIconW toolbox::NotifyIcon
#include "modules/lol/main.cpp"
#undef Shell_NotifyIconW
#undef GetMessageW
#undef WndProc
#undef wWinMain

static void LeagueAction(toolbox::Action action) {
    using namespace toolbox;
    if (action == Primary) SendMessageW(g_app.hwnd, WM_COMMAND, ID_TRAY_AUTO_ACCEPT, 0);
    if (action == Secondary) ManualAccept();
    if (action == Configure) ConfigureAttackSpeedPollInterval();
    if (action == Refresh || action == Primary) {
        std::lock_guard<std::mutex> lock(g_app.stateMutex);
        std::wstring text = g_app.autoAccept ? L"自动接受：开启" : L"自动接受：关闭";
        text += L"\r\n" + g_app.status + L"\r\n" + g_app.detail;
        text += L"\r\n\r\n累计接受：" + std::to_wstring(g_app.acceptedCount);
        text += L"\r\n攻速：" + (g_app.attackSpeedDetected ? std::to_wstring(g_app.attackSpeed) : L"等待游戏");
        text += L"\r\n输出：D:\\LOL.txt";
        text += g_app.attackSpeedOutputWritten ? L"" : L"（写入失败，请检查 D 盘权限）";
        StateText(League, text);
    }
    if (action == Stop && IsWindow(g_app.hwnd)) SendMessageW(g_app.hwnd, WM_COMMAND, ID_TRAY_EXIT, 0);
}
static BOOL LeagueMessage(LPMSG msg, HWND h, UINT low, UINT high) {
    if (toolbox::slots[toolbox::League].window.load() != g_app.hwnd) {
        toolbox::Publish(toolbox::League, g_app.hwnd); LeagueAction(toolbox::Refresh);
    }
    return toolbox::NextMessage(msg, h, low, high, LeagueAction);
}
void toolbox::RunLeague() {
    g_app.exiting = false;
    g_app.autoAccept = !testMode && GetPrivateProfileIntW(L"modules", L"lol_auto_accept", 1,
        (DataDir() + L"\\toolbox.ini").c_str()) != 0;
    LeagueEntry(GetModuleHandleW(nullptr), nullptr, nullptr, SW_HIDE);
    while (g_app.pollRunning || g_app.websocketRunning || g_app.accepting || g_manualWorkers) Sleep(30);
    UnregisterClassW(L"LOLAcceptLiteMessageWindow", GetModuleHandleW(nullptr));
}
