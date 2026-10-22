#include "../src/modules/screenshot/common.h"
#include <winhttp.h>
#include <iostream>
namespace toolbox { std::wstring DataDir(const wchar_t*) {return L".";} }
#include "../src/modules/screenshot/common.cpp"
static std::wstring sentHeaders;
static BOOL WINAPI CaptureSend(HINTERNET,LPCWSTR headers,DWORD,LPVOID,DWORD,DWORD,DWORD_PTR) {
    sentHeaders=headers?headers:L"";SetLastError(ERROR_WINHTTP_CANNOT_CONNECT);return FALSE;
}
#define WinHttpSendRequest CaptureSend
#include "../src/modules/screenshot/translate.cpp"
#undef WinHttpSendRequest
int main() {
    Settings cfg;cfg.llm_base=L"http://127.0.0.1:9/v1";cfg.llm_key=L"unit-test-key";
    translate_text(L"hello",cfg);
    if(sentHeaders.find(L"Authorization: Bearer unit-test-key\r\n")==std::wstring::npos) {
        std::cerr<<"FAIL: configured translation key missing from request authentication header\n";return 1;
    }
    cfg.llm_key=L"  Bearer unit-test-key\r\n";sentHeaders.clear();translate_text(L"hello",cfg);
    if(sentHeaders.find(L"Authorization: Bearer unit-test-key\r\n")==std::wstring::npos) return 2;
    cfg.llm_key=L"unit-test-key\r\nInjected: bad";sentHeaders.clear();auto rejected=translate_text(L"hello",cfg);
    if(!sentHeaders.empty() || rejected.ok())return 3;
    std::cout<<"PASS: configured key reaches actual translation request headers\n";return 0;
}
