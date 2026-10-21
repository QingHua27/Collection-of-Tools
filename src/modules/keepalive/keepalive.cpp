#include "keepalive.h"
#include <set>
#include <algorithm>

namespace tank {
namespace {
bool Same(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }
std::wstring Filename(const std::wstring& path) {
    const auto pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}
std::wstring Directory(const std::wstring& path) {
    const auto pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"" : path.substr(0, pos);
}
std::wstring ProcessPath(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};
    std::wstring path(32768, L'\0');
    DWORD size = static_cast<DWORD>(path.size());
    const bool ok = QueryFullProcessImageNameW(process, 0, path.data(), &size) != FALSE;
    CloseHandle(process);
    if (!ok) return {};
    path.resize(size); return path;
}
std::wstring Class(HWND window) {
    wchar_t value[256]{};
    GetClassNameW(window, value, 256); return value;
}
Candidate Snapshot(HWND root, HWND input) {
    Candidate item{}; item.root = root; item.input = input;
    wchar_t title[512]{}; GetWindowTextW(root, title, 512); item.rootTitle = title;
    DWORD rootPid = 0; GetWindowThreadProcessId(root, &rootPid);
    item.rootPath = ProcessPath(rootPid);
    GetWindowThreadProcessId(input, &item.pid); item.inputPath = ProcessPath(item.pid);
    for (HWND parent = GetParent(input); parent && parent != root; parent = GetParent(parent)) {
        if (Class(parent) == L"CefBrowserWindow") { item.browser = parent; break; }
    }
    RECT rect{};
    if (GetClientRect(input, &rect)) { item.width = rect.right - rect.left; item.height = rect.bottom - rect.top; }
    item.visible = IsWindowVisible(input) != FALSE;
    return item;
}
struct Scan { HWND root; std::vector<Candidate>* items; };
BOOL CALLBACK ChildCallback(HWND child, LPARAM value) {
    auto& scan = *reinterpret_cast<Scan*>(value);
    if (Class(child) == L"Chrome_RenderWidgetHostHWND") scan.items->push_back(Snapshot(scan.root, child));
    return TRUE;
}
BOOL CALLBACK RootCallback(HWND root, LPARAM value) {
    wchar_t title[512]{}; GetWindowTextW(root, title, 512);
    if (std::wstring(title) != L"坦克风暴") return TRUE;
    DWORD pid = 0; GetWindowThreadProcessId(root, &pid);
    if (!Same(Filename(ProcessPath(pid)), L"QQMicroGameBox.exe")) return TRUE;
    Scan scan{root, reinterpret_cast<std::vector<Candidate>*>(value)};
    EnumChildWindows(root, ChildCallback, reinterpret_cast<LPARAM>(&scan));
    return TRUE;
}
}
std::vector<Candidate> SelectTargets(const std::vector<Candidate>& candidates) {
    std::vector<Candidate> result;
    std::set<HWND> seen;
    for (const auto& item : candidates) {
        if (!item.root || !item.browser || !item.input || !item.pid || item.width < 4 || item.height < 4) continue;
        if (item.rootTitle != L"坦克风暴" || !Same(Filename(item.rootPath), L"QQMicroGameBox.exe")) continue;
        if (!Same(Filename(item.inputPath), L"CefBrowserProcess.exe")) continue;
        const auto directory = Directory(item.rootPath);
        if (directory.empty() || !Same(directory, Directory(item.inputPath))) continue;
        if (seen.insert(item.browser).second) result.push_back(item);
    }
    return result;
}
std::vector<Candidate> DiscoverTargets() {
    std::vector<Candidate> candidates;
    EnumWindows(RootCallback, reinterpret_cast<LPARAM>(&candidates));
    return SelectTargets(candidates);
}
Delivery PostMove(HWND input, bool alternate) {
    RECT rect{};
    if (!input || !IsWindow(input) || !GetClientRect(input, &rect) || rect.right < 4 || rect.bottom < 4)
        return {false, ERROR_INVALID_WINDOW_HANDLE};
    SetLastError(ERROR_SUCCESS);
    // The page border may be outside the Flash embed. Hover around the client
    // center so the input reaches the game, without any click or real pointer move.
    // GLOBAL's actual Flash inactivity guard uses abs(mouseX-lastX) > 5.
    // A 1px alternation is ignored. Use 32 client pixels, allowing for DPI scaling.
    const LONG desired = (std::min<LONG>)(rect.right / 2, 32740) + (alternate ? 16 : -16);
    const int x = static_cast<int>((std::clamp<LONG>)(desired, 1, (std::min<LONG>)(rect.right - 2, 32766)));
    const int y = static_cast<int>((std::min<LONG>)(rect.bottom / 2, 32766));
    const bool queued = PostMessageW(input, WM_MOUSEMOVE, 0, MAKELPARAM(x, y)) != FALSE;
    return {queued, queued ? ERROR_SUCCESS : GetLastError()};
}
Delivery PulseTarget(const Candidate& target, bool alternate) {
    if (!IsWindow(target.root) || !IsChild(target.root, target.browser) ||
        !IsChild(target.browser, target.input) || Class(target.browser) != L"CefBrowserWindow" ||
        Class(target.input) != L"Chrome_RenderWidgetHostHWND") return {false, ERROR_INVALID_WINDOW_HANDLE};
    const auto current = Snapshot(target.root, target.input);
    if (current.pid != target.pid || current.browser != target.browser ||
        !Same(current.rootPath, target.rootPath) || !Same(current.inputPath, target.inputPath) ||
        SelectTargets({current}).size() != 1) return {false, ERROR_INVALID_WINDOW_HANDLE};
    return PostMove(target.input, alternate);
}
}
