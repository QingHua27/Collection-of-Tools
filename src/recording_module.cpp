#include "gateway.h"
#include <stdexcept>
static DWORD recordingWorker=0;
static void RecordingQuit(int code) { PostThreadMessageW(recordingWorker,WM_QUIT,code,0); }
#define PostQuitMessage RecordingQuit
static BOOL RecordingMessage(LPMSG, HWND, UINT, UINT);
#define wWinMain RecordingEntry
#define GetMessageW RecordingMessage
#define Shell_NotifyIconW toolbox::NotifyIcon
#include "modules/recorder/main.cpp"
#undef Shell_NotifyIconW
#undef GetMessageW
#undef wWinMain
#undef PostQuitMessage

static void RecordingActionImpl(toolbox::Action action) {
    using namespace toolbox;
    if (action == Primary) PostMessageW(g_main, WM_COMMAND, IDC_RECORD, 0);
    if (action == Secondary) ExitFloatMode();
    if (action == TestRecording) {
        if(GetWindowThreadProcessId(g_main,nullptr)!=GetWindowThreadProcessId(host,nullptr))
            throw std::runtime_error("recorder controls must share host input thread");
        for(HWND combo : {g_cmbTarget,g_cmbEncoder}) {
            if(GetWindowThreadProcessId(combo,nullptr)!=GetWindowThreadProcessId(host,nullptr) ||
               !IsWindowEnabled(combo) || SendMessageW(combo,CB_GETCOUNT,0,0)<2)
                throw std::runtime_error("recorder dropdown unavailable");
            SendMessageW(combo,CB_SETCURSEL,1,0);
            if(SendMessageW(combo,CB_GETCURSEL,0,0)!=1)
                throw std::runtime_error("recorder dropdown selection failed");
        }
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
static void RecordingAction(toolbox::Action action) { toolbox::OnHost([&] { RecordingActionImpl(action); }); }
static BOOL RecordingMessage(LPMSG msg, HWND h, UINT low, UINT high) {
    static bool published = false;
    if (!published) { published = true; toolbox::Publish(toolbox::Recording, g_main, true); RecordingAction(toolbox::Refresh); }
    return toolbox::NextMessage(msg, h, low, high, RecordingAction);
}
void toolbox::RunRecording() { recordingWorker=GetCurrentThreadId(); RecordingEntry(GetModuleHandleW(nullptr), nullptr, nullptr, SW_HIDE); }
