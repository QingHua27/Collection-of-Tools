#include "preview.h"
#include <objidl.h>
#include <gdiplus.h>

bool SaveWindowPreview(HWND window, const std::wstring& path) {
    RECT r{}; GetWindowRect(window,&r);
    int width=r.right-r.left, height=r.bottom-r.top;
    if(width<=0 || height<=0) return false;
    HDC screen=GetDC(window), dc=CreateCompatibleDC(screen);
    HBITMAP bitmap=CreateCompatibleBitmap(screen,width,height);
    auto old=SelectObject(dc,bitmap);
    BOOL captured=PrintWindow(window,dc,PW_RENDERFULLCONTENT);
    Gdiplus::GdiplusStartupInput input; ULONG_PTR token=0;
    bool okay=false;
    if(captured && Gdiplus::GdiplusStartup(&token,&input,nullptr)==Gdiplus::Ok) {
        {
            Gdiplus::Bitmap image(bitmap,nullptr);
            CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
            okay=image.Save(path.c_str(),&png,nullptr)==Gdiplus::Ok;
        }
        Gdiplus::GdiplusShutdown(token);
    }
    SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(window,screen);
    return okay;
}
