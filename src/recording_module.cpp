#include "gateway.h"
static BOOL RecordingMessage(LPMSG, HWND, UINT, UINT);
#define wWinMain RecordingEntry
#define GetMessageW RecordingMessage
#define Shell_NotifyIconW toolbox::NotifyIcon
#include "modules/recorder/main.cpp"
#undef Shell_NotifyIconW
#undef GetMessageW
#undef wWinMain

static void RecordingAction(toolbox::Action action) {
    using namespace toolbox;
    if (action == Primary) PostMessageW(g_main, WM_COMMAND, IDC_RECORD, 0);
    if (action == Secondary) ExitFloatMode();
    if (action == TestRecording) {
        SendMessageW(g_cmbTarget, CB_SETCURSEL, 0, 0);
        SendMessageW(g_cmbTarget, WM_COMMAND, 0, 0);
        SendMessageW(g_chkSystemAudio, BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(g_chkMicrophone, BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(g_cmbEncoder, CB_SETCURSEL, 2, 0);
        SetWindowTextW(g_edtDir, DataDir(L"test-recordings").c_str());
        PostMessageW(g_main, WM_COMMAND, IDC_RECORD, 0);
    }
    if (action == TestRecordingStop && g_rec.Running()) PostMessageW(g_main, WM_COMMAND, IDC_RECORD, 0);
    if (action == Refresh) StateText(Recording, g_rec.Running() ? L"正在录制，退出时会先保存视频。" : L"录屏就绪 · 支持显示器、区域、窗口和系统/麦克风音频。");
    if (action == Stop && IsWindow(g_main)) ExitApplication();
}
static BOOL RecordingMessage(LPMSG msg, HWND h, UINT low, UINT high) {
    static bool published = false;
    if (!published) { published = true; toolbox::Publish(toolbox::Recording, g_main, true); RecordingAction(toolbox::Refresh); }
    return toolbox::NextMessage(msg, h, low, high, RecordingAction);
}
void toolbox::RunRecording() { RecordingEntry(GetModuleHandleW(nullptr), nullptr, nullptr, SW_HIDE); }
