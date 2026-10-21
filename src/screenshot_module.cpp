#include "gateway.h"
#include "theme.h"
static BOOL ScreenshotMessage(LPMSG, HWND, UINT, UINT);
#define wWinMain ScreenshotEntry
#define GetMessageW ScreenshotMessage
#define Shell_NotifyIconW toolbox::NotifyIcon
#include "modules/screenshot/main.cpp"
#undef Shell_NotifyIconW
#undef GetMessageW
#undef wWinMain
std::atomic<int> toolbox::screenshotPreset{0};
std::atomic<bool> toolbox::screenshotSelectionVerified{false};
const wchar_t* toolbox::ScreenshotPresetName(int index) { return HOTKEY_PRESETS[index].name; }

static void ScreenshotAction(toolbox::Action action) {
    using namespace toolbox;
    if (action == Primary) overlay_open();
    if (action == Configure) {if(testMode)screenshotSelectionVerified=overlay_selection_self_test();show_settings_dialog(toolbox::host);}
    int chosen=static_cast<int>(action)-static_cast<int>(HotkeyFirst);
    if(chosen>=0 && chosen<N_HOTKEY_PRESETS) {
        UINT oldMods=g_hotMods, oldVk=g_hotVk;
        const auto& preset=HOTKEY_PRESETS[chosen];
        if(testMode || try_register(preset.mods,preset.vk)) {
            g_hotName=preset.name; g_cfg.hotkey=g_hotName;
            screenshotPreset=chosen; save_settings(g_cfg);
        } else {
            try_register(oldMods,oldVk);
            NOTIFYICONDATAW notice{}; notice.uFlags=NIF_INFO; notice.dwInfoFlags=NIIF_WARNING;
            wcscpy_s(notice.szInfoTitle,L"截图快捷键设置失败");
            wcscpy_s(notice.szInfo,L"该组合可能已被其他程序占用，已尝试恢复原快捷键。请选择其他组合。");
            NotifyIcon(NIM_MODIFY,&notice);
        }
    }
    if (action == Refresh)
        StateText(Screenshot, (testMode ? std::wstring(L"测试模式：未注册全局快捷键") :
            (g_hotOk ? L"截图快捷键：" : L"快捷键注册失败：") + g_hotName) +
            L"\r\n\r\n框选、标注、马赛克、贴图、文字识别和翻译均可使用。\r\nOCR 在本地运行；翻译使用所选在线服务。");
    if (action == Stop && IsWindow(g_msgwnd)) DestroyWindow(g_msgwnd);
}
static BOOL ScreenshotMessage(LPMSG msg, HWND h, UINT low, UINT high) {
    static bool published = false;
    if (!published) {
        published=true;
        for(int i=0;i<N_HOTKEY_PRESETS;++i) if(g_hotName==HOTKEY_PRESETS[i].name) toolbox::screenshotPreset=i;
        toolbox::Publish(toolbox::Screenshot,g_msgwnd); ScreenshotAction(toolbox::Refresh);
    }
    return toolbox::NextMessage(msg, h, low, high, ScreenshotAction);
}
void toolbox::RunScreenshot() { ScreenshotEntry(GetModuleHandleW(nullptr), nullptr, nullptr, SW_HIDE); }
