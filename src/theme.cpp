#include "theme.h"
#include "gateway.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <objidl.h>
#include <gdiplus.h>
#include <cmath>
#include <algorithm>
#include <memory>
namespace toolbox::theme {
namespace {
unsigned backgroundBuilds=0;
void GraphicsRuntime() { static ULONG_PTR token=[] { ULONG_PTR t=0;Gdiplus::GdiplusStartupInput input;Gdiplus::GdiplusStartup(&t,&input,nullptr);return t;}(); }
Gdiplus::Image* Artwork() {
    GraphicsRuntime();
    static auto image=[]()->Gdiplus::Image* {
        auto r=FindResourceW(nullptr,MAKEINTRESOURCEW(404),RT_RCDATA);if(!r)return nullptr;
        auto size=SizeofResource(nullptr,r);auto bytes=LockResource(LoadResource(nullptr,r));
        auto mem=GlobalAlloc(GMEM_MOVEABLE,size);if(!mem)return nullptr;
        auto data=GlobalLock(mem);if(!data){GlobalFree(mem);return nullptr;}memcpy(data,bytes,size);GlobalUnlock(mem);
        IStream* stream=nullptr;if(FAILED(CreateStreamOnHGlobal(mem,TRUE,&stream))){GlobalFree(mem);return nullptr;}
        // GDI+ lazily reads the image, so the backing stream lives for the process lifetime.
        return Gdiplus::Image::FromStream(stream);
    }();return image;
}
HDC CachedPaper(HDC target,int width,int height) {
    struct Cache {
        HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;int width=0,height=0;
        void Clear() {if(dc){SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);}dc=nullptr;}
        ~Cache(){Clear();}
    };
    static thread_local Cache cache;
    if(width<=0 || height<=0)return nullptr;
    if(cache.dc && cache.width==width && cache.height==height)return cache.dc;
    cache.Clear();cache.dc=CreateCompatibleDC(target);cache.bitmap=CreateCompatibleBitmap(target,width,height);
    if(!cache.dc || !cache.bitmap){if(cache.bitmap)DeleteObject(cache.bitmap);if(cache.dc)DeleteDC(cache.dc);cache.dc=nullptr;return nullptr;}
    cache.previous=SelectObject(cache.dc,cache.bitmap);cache.width=width;cache.height=height;
    RECT r{0,0,width,height};FillRect(cache.dc,&r,(HBRUSH)GetStockObject(WHITE_BRUSH));
    Gdiplus::Graphics g(cache.dc);if(auto art=Artwork())g.DrawImage(art,0,0,width,height);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::SolidBrush sidebar(Gdiplus::Color(145,250,249,241));g.FillRectangle(&sidebar,0,0,(int)(width*.195f),height);
    float x=width*.21f,y=height*.081f,ww=width*.775f,hh=height*.86f,rr=20.f;
    Gdiplus::GraphicsPath card;card.AddArc(x,y,rr,rr,180,90);card.AddArc(x+ww-rr,y,rr,rr,270,90);card.AddArc(x+ww-rr,y+hh-rr,rr,rr,0,90);card.AddArc(x,y+hh-rr,rr,rr,90,90);card.CloseFigure();
    Gdiplus::SolidBrush paper(Gdiplus::Color(75,255,255,251));Gdiplus::Pen edge(Gdiplus::Color(170,213,217,199),1.f);g.FillPath(&paper,&card);g.DrawPath(&edge,&card);
    ++backgroundBuilds;return cache.dc;
}
void PaperAt(HWND h,HDC dc,RECT r) {
    auto root=GetAncestor(h,GA_ROOT);RECT whole{};GetClientRect(root,&whole);POINT p{};MapWindowPoints(h,root,&p,1);
    if(auto cached=CachedPaper(dc,whole.right,whole.bottom))BitBlt(dc,0,0,r.right,r.bottom,cached,p.x,p.y,SRCCOPY);
}
HBRUSH PaperBrush() { static HBRUSH b=CreateSolidBrush(Paper); return b; }
HBRUSH CardBrush() { static HBRUSH b=CreateSolidBrush(Card); return b; }
void Leaf(Gdiplus::Graphics& g,float x,float y,float size,float angle,int alpha) {
    auto save=g.Save(); g.TranslateTransform(x,y); g.RotateTransform(angle);
    Gdiplus::GraphicsPath p;
    p.AddBezier(0.f,0.f,-size*.55f,-size*.42f,-size*.35f,-size*.95f,0.f,-size);
    p.AddBezier(0.f,-size,size*.38f,-size*.7f,size*.5f,-size*.2f,0.f,0.f);
    Gdiplus::SolidBrush fill(Gdiplus::Color(alpha,108,135,87)); g.FillPath(&fill,&p);
    Gdiplus::Pen vein(Gdiplus::Color(alpha+20,105,123,80),.7f); g.DrawLine(&vein,0.f,0.f,0.f,-size*.88f);
    g.Restore(save);
}
void Branch(Gdiplus::Graphics& g,float x,float y,float scale,float rotation) {
    auto save=g.Save(); g.TranslateTransform(x,y); g.ScaleTransform(scale,scale); g.RotateTransform(rotation);
    Gdiplus::Pen stem(Gdiplus::Color(125,122,131,88),1.3f);
    g.DrawBezier(&stem,0.f,0.f,28.f,-62.f,-20.f,-137.f,34.f,-220.f);
    for(int i=0;i<8;++i) {
        float yy=-22.f-i*24.f, xx=8.f+std::sin(i*1.2f)*9.f;
        float side=i%2?1.f:-1.f;
        g.DrawLine(&stem,xx,yy,xx+side*23.f,yy-13.f);
        Leaf(g,xx+side*23.f,yy-13.f,35.f+i%3*5.f,side*48.f,65+i%3*18);
        Leaf(g,xx,yy,25.f,side*-25.f,55);
        if(i%3==0) { Gdiplus::SolidBrush blossom(Gdiplus::Color(95,181,159,91));
            for(int k=0;k<3;++k)g.FillEllipse(&blossom,xx+side*(30.f+k*4),yy+8.f-k*5,5.f,7.f); }
    }
    g.Restore(save);
}
LRESULT CALLBACK Styled(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR) {
    if(m==WM_NCDESTROY) { RemovePropW(h,L"IntegrationPaintCount");RemovePropW(h,L"BotanicalHover"); RemoveWindowSubclass(h,Styled,0x734); return DefSubclassProc(h,m,w,l); }
    if(m==WM_PAINT && toolbox::testMode)SetPropW(h,L"IntegrationPaintCount",(HANDLE)((ULONG_PTR)GetPropW(h,L"IntegrationPaintCount")+1));
    wchar_t cls[64]{}; GetClassNameW(h,cls,64);
    if(m==WM_ERASEBKGND && (wcscmp(cls,L"LiteCapMainWnd")==0 || wcscmp(cls,L"CppToolbox.Page")==0 ||
        wcscmp(cls,L"Button")==0 || wcscmp(cls,L"ComboBox")==0))return 1;
    if((m==WM_PAINT || m==WM_PRINTCLIENT) && wcscmp(cls,L"Static")==0 && GetAncestor(h,GA_ROOT)==toolbox::host) {
        PAINTSTRUCT ps{};HDC dc=m==WM_PAINT?BeginPaint(h,&ps):(HDC)w;RECT r{};GetClientRect(h,&r);PaperAt(h,dc,r);
        wchar_t text[1024]{};GetWindowTextW(h,text,1024);auto font=(HFONT)SendMessageW(h,WM_GETFONT,0,0);
        if(font)SelectObject(dc,font);SetTextColor(dc,Ink);SetBkMode(dc,TRANSPARENT);
        DrawTextW(dc,text,-1,&r,((GetWindowLongPtrW(h,GWL_STYLE)&SS_TYPEMASK)==SS_CENTER?DT_CENTER:DT_LEFT)|DT_WORDBREAK);if(m==WM_PAINT)EndPaint(h,&ps);return 0;
    }
    if(m==WM_NOTIFY && reinterpret_cast<NMHDR*>(l)->code==NM_CUSTOMDRAW) {
        auto header=reinterpret_cast<NMHDR*>(l);wchar_t childClass[64]{};GetClassNameW(header->hwndFrom,childClass,64);
        if(wcscmp(childClass,WC_LISTVIEWW)==0) {
            auto draw=reinterpret_cast<NMLVCUSTOMDRAW*>(l);
            if(draw->nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
            if(draw->nmcd.dwDrawStage==CDDS_ITEMPREPAINT) {draw->clrText=RGB(41,42,49);draw->clrTextBk=RGB(253,252,248);return CDRF_NOTIFYPOSTPAINT;}
            if(draw->nmcd.dwDrawStage==CDDS_ITEMPOSTPAINT) {auto p=CreatePen(PS_SOLID,1,RGB(231,230,221));auto old=SelectObject(draw->nmcd.hdc,p);
                MoveToEx(draw->nmcd.hdc,draw->nmcd.rc.left,draw->nmcd.rc.bottom-1,nullptr);LineTo(draw->nmcd.hdc,draw->nmcd.rc.right,draw->nmcd.rc.bottom-1);
                SelectObject(draw->nmcd.hdc,old);DeleteObject(p);return CDRF_DODEFAULT;}
        }
    }
    if((m==WM_PAINT || m==WM_PRINTCLIENT) && wcscmp(cls,L"ComboBox")==0) {
        PAINTSTRUCT ps{};HDC dc=m==WM_PAINT?BeginPaint(h,&ps):(HDC)w;RECT r{};GetClientRect(h,&r);FillRect(dc,&r,CardBrush());
        auto pen=CreatePen(PS_SOLID,1,Border);auto old=SelectObject(dc,pen);auto brush=SelectObject(dc,PaperBrush());
        RoundRect(dc,0,0,r.right,r.bottom,8,8);SelectObject(dc,old);SelectObject(dc,brush);DeleteObject(pen);
        wchar_t text[512]{};GetWindowTextW(h,text,512);auto font=(HFONT)SendMessageW(h,WM_GETFONT,0,0);if(font)SelectObject(dc,font);
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,IsWindowEnabled(h)?RGB(38,39,44):Muted);
        RECT label=r;label.left+=12;label.right-=32;DrawTextW(dc,text,-1,&label,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        auto arrow=CreatePen(PS_SOLID,1,Ink);old=SelectObject(dc,arrow);int x=r.right-21,y=r.bottom/2;
        MoveToEx(dc,x-4,y-2,nullptr);LineTo(dc,x,y+2);LineTo(dc,x+4,y-2);SelectObject(dc,old);DeleteObject(arrow);
        if(m==WM_PAINT)EndPaint(h,&ps);return 0;
    }
    if((m==WM_PAINT || m==WM_PRINTCLIENT) && wcscmp(cls,WC_HEADERW)==0) {
        PAINTSTRUCT ps{};HDC dc=m==WM_PAINT?BeginPaint(h,&ps):(HDC)w;RECT all{};GetClientRect(h,&all);
        FillRect(dc,&all,PaperBrush());auto font=(HFONT)SendMessageW(h,WM_GETFONT,0,0);if(font)SelectObject(dc,font);
        SetTextColor(dc,RGB(41,42,49));SetBkMode(dc,TRANSPARENT);
        for(int i=0;i<Header_GetItemCount(h);++i) {RECT cell{};Header_GetItemRect(h,i,&cell);wchar_t text[100]{};
            HDITEMW item{};item.mask=HDI_TEXT;item.pszText=text;item.cchTextMax=100;Header_GetItem(h,i,&item);cell.left+=16;
            DrawTextW(dc,text,-1,&cell,DT_LEFT|DT_SINGLELINE|DT_VCENTER);}
        if(m==WM_PAINT)EndPaint(h,&ps);return 0;
    }
    if((m==WM_PAINT || m==WM_PRINTCLIENT) && wcscmp(cls,UPDOWN_CLASSW)==0) {
        PAINTSTRUCT ps{};HDC dc=m==WM_PAINT?BeginPaint(h,&ps):(HDC)w;RECT r{};GetClientRect(h,&r);FillRect(dc,&r,CardBrush());
        auto pen=CreatePen(PS_SOLID,1,Muted);auto old=SelectObject(dc,pen);int x=r.right/2;
        for(int y:{r.bottom/4,r.bottom*3/4}) {int sign=y<r.bottom/2?1:-1;MoveToEx(dc,x-3,y+sign,nullptr);LineTo(dc,x,y-sign*2);LineTo(dc,x+3,y+sign);}
        SelectObject(dc,old);DeleteObject(pen);if(m==WM_PAINT)EndPaint(h,&ps);return 0;
    }
    if(m==WM_SIZE && wcscmp(cls,L"Edit")==0) {
        auto result=DefSubclassProc(h,m,w,l);RECT r{};GetWindowRect(h,&r);SetWindowRgn(h,CreateRoundRectRgn(0,0,r.right-r.left+1,r.bottom-r.top+1,12,12),FALSE);return result;
    }
    if(m==WM_PAINT && wcscmp(cls,L"Edit")==0) {
        auto result=DefSubclassProc(h,m,w,l);HDC dc=GetWindowDC(h);RECT r{};GetWindowRect(h,&r);
        auto pen=CreatePen(PS_SOLID,1,GetFocus()==h?Green:Border);auto op=SelectObject(dc,pen);auto ob=SelectObject(dc,GetStockObject(NULL_BRUSH));
        RoundRect(dc,0,0,r.right-r.left,r.bottom-r.top,7,7);SelectObject(dc,op);SelectObject(dc,ob);DeleteObject(pen);ReleaseDC(h,dc);return result;
    }
    if(m==WM_CTLCOLORSTATIC || m==WM_CTLCOLORBTN || m==WM_CTLCOLOREDIT || m==WM_CTLCOLORLISTBOX) {
        bool host=wcsstr(cls,L"CppToolbox.Main")!=nullptr;
        SetTextColor((HDC)w,Ink); SetBkColor((HDC)w,host?Paper:Card); SetBkMode((HDC)w,TRANSPARENT);
        return (LRESULT)(host?PaperBrush():CardBrush());
    }
    if(m==WM_PAINT && (wcscmp(cls,L"#32770")==0 || wcscmp(cls,L"JianJieSettings")==0 || wcscmp(cls,L"LiteCapMainWnd")==0 || wcscmp(cls,L"CppToolbox.Page")==0)) {
        PAINTSTRUCT ps{}; HDC dc=m==WM_PAINT?BeginPaint(h,&ps):(HDC)w;
        RECT r{}; GetClientRect(h,&r);
        if(wcscmp(cls,L"#32770")==0 || wcscmp(cls,L"JianJieSettings")==0)FillRect(dc,&r,CardBrush());else PaperAt(h,dc,r);
        Gdiplus::Graphics canvas(dc);canvas.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        auto box=[&](RECT b) {Gdiplus::GraphicsPath shape;float x=b.left,y=b.top,ww=b.right-b.left,hh=b.bottom-b.top,rr=14.f;
            shape.AddArc(x,y,rr,rr,180,90);shape.AddArc(x+ww-rr,y,rr,rr,270,90);shape.AddArc(x+ww-rr,y+hh-rr,rr,rr,0,90);shape.AddArc(x,y+hh-rr,rr,rr,90,90);shape.CloseFigure();
            Gdiplus::SolidBrush card(Gdiplus::Color(238,253,252,247));Gdiplus::Pen edge(Gdiplus::Color(220,205,205,187),1.f);canvas.FillPath(&card,&shape);canvas.DrawPath(&edge,&shape);};
        if(wcscmp(cls,L"LiteCapMainWnd")==0) {
            int split=std::max(MulDiv(273,GetDpiForWindow(h),96),(int)r.right*44/100);
            box({4,4,split-6,r.bottom-5});box({split+7,4,r.right-4,r.bottom-5});
            auto font=(HFONT)SendMessageW(GetWindow(h,GW_CHILD),WM_GETFONT,0,0);
            if(font)SelectObject(dc,font);SetTextColor(dc,Ink);SetBkMode(dc,TRANSPARENT);
            int d=GetDpiForWindow(h);
            RECT left{MulDiv(20,d,96),MulDiv(10,d,96),split-15,MulDiv(38,d,96)};
            RECT right{split+MulDiv(20,d,96),MulDiv(10,d,96),r.right-15,MulDiv(38,d,96)};
            auto heading=CreateFontW(-MulDiv(17,d,96),0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"SimSun");auto old=SelectObject(dc,heading);
            Icon(dc,{left.left,left.top,left.left+28,left.bottom},0);left.left+=40;
            Icon(dc,{right.left,right.top,right.left+25,right.bottom},5);right.left+=36;
            DrawTextW(dc,L"录制设置",-1,&left,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            wchar_t label[80]{};swprintf_s(label,L"录制文件  (%d)",ListView_GetItemCount(GetDlgItem(h,GetDlgCtrlID(FindWindowExW(h,nullptr,WC_LISTVIEWW,nullptr)))));
            DrawTextW(dc,label,-1,&right,DT_LEFT|DT_VCENTER|DT_SINGLELINE);SelectObject(dc,old);DeleteObject(heading);
            HPEN line=CreatePen(PS_SOLID,1,Border);auto oldpen=SelectObject(dc,line);MoveToEx(dc,left.left-40,left.bottom+6,nullptr);LineTo(dc,split-24,left.bottom+6);SelectObject(dc,oldpen);DeleteObject(line);
        }
        if(m==WM_PAINT)EndPaint(h,&ps);return m==WM_PAINT?0:1;
    }
    if(wcscmp(cls,L"Button")==0) {
        if(m==WM_MOUSEMOVE && !GetPropW(h,L"BotanicalHover") && IsWindowEnabled(h)) {
            SetPropW(h,L"BotanicalHover",(HANDLE)1);TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,h,0};TrackMouseEvent(&tracking);InvalidateRect(h,nullptr,FALSE);
        }
        if(m==WM_MOUSELEAVE || m==WM_CANCELMODE || m==WM_ENABLE) {RemovePropW(h,L"BotanicalHover");InvalidateRect(h,nullptr,FALSE);}
        if(m==WM_LBUTTONDOWN)SendMessageW(GetAncestor(h,GA_ROOT),WM_CHANGEUISTATE,MAKEWPARAM(UIS_SET,UISF_HIDEFOCUS),0);
        if(m==WM_UPDATEUISTATE) {auto result=DefSubclassProc(h,m,w,l);InvalidateRect(h,nullptr,FALSE);return result;}
        auto kind=GetWindowLongPtrW(h,GWL_STYLE)&BS_TYPEMASK;
        if((m==WM_PAINT || m==WM_PRINTCLIENT) && kind!=BS_OWNERDRAW && kind!=BS_GROUPBOX) {
            PAINTSTRUCT ps{}; HDC dc=m==WM_PAINT?BeginPaint(h,&ps):(HDC)w; RECT r{};GetClientRect(h,&r);
            PaperAt(h,dc,r);
            wchar_t text[256]{};GetWindowTextW(h,text,256);
            bool check=kind==BS_AUTOCHECKBOX || kind==BS_CHECKBOX || kind==BS_AUTORADIOBUTTON || kind==BS_RADIOBUTTON;
            bool primary=GetDlgCtrlID(h)==300 || GetDlgCtrlID(h)==330 || GetDlgCtrlID(h)==340 || GetDlgCtrlID(h)==350 ||
                wcsstr(text,L"开始") || wcsstr(text,L"停止录制") || wcsstr(text,L"开 始");
            bool enabled=IsWindowEnabled(h)!=FALSE;
            bool down=(SendMessageW(h,BM_GETSTATE,0,0)&BST_PUSHED)!=0;
            bool hover=Hovered(h) && enabled;
            COLORREF bg=primary?(down?Ink:hover?RGB(82,121,70):Green):(down?RGB(199,214,181):hover?RGB(233,239,220):Paper);
            HPEN pen=CreatePen(PS_SOLID,1,Border); HBRUSH brush=CreateSolidBrush(bg);
            auto oldp=SelectObject(dc,pen),oldb=SelectObject(dc,brush);
            if(check) {
                bool selected=SendMessageW(h,BM_GETCHECK,0,0)==BST_CHECKED;
                if(selected) { DeleteObject(SelectObject(dc,CreateSolidBrush(Green))); }
                int side=std::min(14,(int)r.bottom-4), y=(r.bottom-side)/2;
                if(kind==BS_AUTORADIOBUTTON || kind==BS_RADIOBUTTON) Ellipse(dc,1,y,side+1,y+side);
                else RoundRect(dc,1,y,side+1,y+side,5,5);
                if(selected) {
                    if(kind==BS_AUTORADIOBUTTON || kind==BS_RADIOBUTTON) {
                        GraphicsRuntime();Gdiplus::Graphics g(dc);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                        Gdiplus::SolidBrush dot(Gdiplus::Color(255,255,255,245));g.FillEllipse(&dot,4.f,(float)y+3,(float)side-6,(float)side-6);
                    } else {HPEN tick=CreatePen(PS_SOLID,2,RGB(255,255,245));auto previous=SelectObject(dc,tick);
                        MoveToEx(dc,4,y+side/2,nullptr);LineTo(dc,side/2,y+side-4);LineTo(dc,side-3,y+4);SelectObject(dc,previous);DeleteObject(tick);}
                }
                r.left+=side+9;
            } else { r.left+=GetDlgCtrlID(h)==1205 || GetDlgCtrlID(h)==1206?2:8; r.right-=GetDlgCtrlID(h)==1205 || GetDlgCtrlID(h)==1206?2:8; }
            if(!check) {RECT full{};GetClientRect(h,&full);Rounded(dc,full,bg,8.f);}
            auto actual=SelectObject(dc,oldb); DeleteObject(actual); SelectObject(dc,oldp); DeleteObject(pen);
            auto font=(HFONT)SendMessageW(h,WM_GETFONT,0,0); if(font)SelectObject(dc,font);
            SetBkMode(dc,TRANSPARENT); SetTextColor(dc,!enabled?Muted:primary&&!check?RGB(255,255,244):Ink);
            if(GetDlgCtrlID(h)==1201) {
                int x=r.right-51,y=r.bottom/2;bool on=wcsstr(text,L"开")!=nullptr;
                GraphicsRuntime();Gdiplus::Graphics g(dc);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                Gdiplus::GraphicsPath track;track.AddArc((float)x,(float)y-12,24.f,24.f,90.f,180.f);track.AddArc((float)x+20,(float)y-12,24.f,24.f,270.f,180.f);track.CloseFigure();
                Gdiplus::SolidBrush fill(Gdiplus::Color(255,on?67:111,on?103:123,on?63:101)),knob(Gdiplus::Color(255,255,255,248));
                g.FillPath(&fill,&track);int cx=on?x+31:x+13;g.FillEllipse(&knob,(float)cx-9.5f,(float)y-9.5f,19.f,19.f);
                r.right=x-8;wcscpy_s(text,L"自启");
            }
            if(primary && !check && (wcsstr(text,L"开始录制") || wcsstr(text,L"停止录制"))) {
                auto pen=CreatePen(PS_SOLID,2,RGB(255,255,245));auto old=SelectObject(dc,pen);auto b=SelectObject(dc,GetStockObject(NULL_BRUSH));
                int x=r.left+22,y=r.bottom/2;Ellipse(dc,x-13,y-13,x+13,y+13);Ellipse(dc,x-8,y-8,x+8,y+8);
                SelectObject(dc,old);SelectObject(dc,b);DeleteObject(pen);r.left+=44;
            }
            if(down)OffsetRect(&r,0,1);
            DrawTextW(dc,text,-1,&r,(check?DT_LEFT:DT_CENTER)|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            if(GetFocus()==h && !(SendMessageW(h,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS)) {RECT focus{};GetClientRect(h,&focus);InflateRect(&focus,-2,-2);FocusRing(dc,focus,7.f);}
            if(m==WM_PAINT)EndPaint(h,&ps);return 0;
        }
        if(m==BM_SETCHECK || m==BM_SETSTATE || m==WM_ENABLE || m==WM_SETFOCUS || m==WM_KILLFOCUS) {
            auto result=DefSubclassProc(h,m,w,l); InvalidateRect(h,nullptr,FALSE);return result;
        }
    }
    return DefSubclassProc(h,m,w,l);
}
void One(HWND h) {
    SetWindowSubclass(h,Styled,0x734,0);
    wchar_t cls[64]{};GetClassNameW(h,cls,64);
    if(wcscmp(cls,WC_LISTVIEWW)==0) {
        ListView_SetBkColor(h,Card);ListView_SetTextBkColor(h,Card);ListView_SetTextColor(h,Ink);
        if(!GetPropW(h,L"BotanicalRowHeight")) {
            auto images=ImageList_Create(1,MulDiv(34,GetDpiForWindow(h),96),ILC_COLOR32,1,1);
            ListView_SetImageList(h,images,LVSIL_SMALL);SetPropW(h,L"BotanicalRowHeight",images);
        }
    }
    if(wcscmp(cls,L"ComboBox")==0) {SetWindowTheme(h,L"",L"");SendMessageW(h,CB_SETITEMHEIGHT,-1,MulDiv(23,GetDpiForWindow(h),96));}
    if(wcscmp(cls,L"Button")==0)SetWindowTheme(h,L"",L"");
    InvalidateRect(h,nullptr,TRUE);
}
}
void Apply(HWND root) {
    if(!IsWindow(root))return;
    // Subclass only windows owned by this input thread.
    if(GetWindowThreadProcessId(root,nullptr)!=GetCurrentThreadId())return;
    One(root);EnumChildWindows(root,[](HWND h,LPARAM)->BOOL {
        if(GetWindowThreadProcessId(h,nullptr)==GetCurrentThreadId())One(h);return TRUE;
    },0);
}
void Dialog(HWND window,HWND owner) {
    static thread_local HFONT font=CreateFontW(-MulDiv(13,GetDpiForWindow(window),96),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
    EnumChildWindows(window,[](HWND child,LPARAM f)->BOOL {SendMessageW(child,WM_SETFONT,f,TRUE);return TRUE;},(LPARAM)font);
    Apply(window);
    RECT r{},o{};GetWindowRect(window,&r);if(owner && GetWindowRect(owner,&o))SetWindowPos(window,nullptr,o.left+((o.right-o.left)-(r.right-r.left))/2,o.top+((o.bottom-o.top)-(r.bottom-r.top))/2,0,0,SWP_NOSIZE|SWP_NOZORDER);
    ShowWindow(window,SW_SHOW);toolbox::OnHost([window]{SetWindowPos(window,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);SetForegroundWindow(window);});
}
void PaintSurface(HWND window,HDC dc,RECT bounds) {PaperAt(window,dc,bounds);}
unsigned BackgroundBuilds(){return backgroundBuilds;}
void PaintBackdrop(HDC dc,RECT r,bool foliage) {
    if(auto cached=CachedPaper(dc,r.right,r.bottom))BitBlt(dc,0,0,r.right,r.bottom,cached,0,0,SRCCOPY);
}
bool Hovered(HWND window){return GetPropW(window,L"BotanicalHover")!=nullptr;}
void FocusRing(HDC dc,RECT b,float radius) {
    GraphicsRuntime();Gdiplus::Graphics g(dc);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    float x=(float)b.left+.5f,y=(float)b.top+.5f,w=(float)(b.right-b.left)-1,h=(float)(b.bottom-b.top)-1,rr=radius*2;
    Gdiplus::GraphicsPath p;p.AddArc(x,y,rr,rr,180,90);p.AddArc(x+w-rr,y,rr,rr,270,90);p.AddArc(x+w-rr,y+h-rr,rr,rr,0,90);p.AddArc(x,y+h-rr,rr,rr,90,90);p.CloseFigure();
    Gdiplus::Pen pen(Gdiplus::Color(255,96,131,76),1.5f);g.DrawPath(&pen,&p);
}
void Rounded(HDC dc,RECT b,COLORREF color,float radius) {
    GraphicsRuntime();Gdiplus::Graphics g(dc);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    float x=(float)b.left+.5f,y=(float)b.top+.5f,w=(float)(b.right-b.left)-1,h=(float)(b.bottom-b.top)-1,rr=radius*2;
    Gdiplus::GraphicsPath p;p.AddArc(x,y,rr,rr,180,90);p.AddArc(x+w-rr,y,rr,rr,270,90);p.AddArc(x+w-rr,y+h-rr,rr,rr,0,90);p.AddArc(x,y+h-rr,rr,rr,90,90);p.CloseFigure();
    Gdiplus::SolidBrush fill(Gdiplus::Color(255,GetRValue(color),GetGValue(color),GetBValue(color)));if(color==Green) {Gdiplus::LinearGradientBrush gradient(Gdiplus::PointF(x,y),Gdiplus::PointF(x,y+h),Gdiplus::Color(255,91,137,79),Gdiplus::Color(255,53,91,46));g.FillPath(&gradient,&p);} else g.FillPath(&fill,&p);
}
void Icon(HDC dc,RECT b,int kind) {
    GraphicsRuntime();Gdiplus::Graphics g(dc);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    auto save=g.Save();g.TranslateTransform((float)b.left,(float)b.top);g.ScaleTransform((b.right-b.left)/48.f,(b.bottom-b.top)/48.f);
    Gdiplus::Pen ink(Gdiplus::Color(255,68,96,61),2.6f),light(Gdiplus::Color(255,253,252,238),2.2f);
    ink.SetLineJoin(Gdiplus::LineJoinRound);Gdiplus::SolidBrush fill(Gdiplus::Color(255,78,111,67));
    if(kind<0) {g.FillRectangle(&fill,4,16,40,27);g.DrawRectangle(&ink,4,16,40,27);g.DrawRectangle(&ink,15,6,18,11);
        g.DrawLine(&light,5,26,43,26);g.DrawRectangle(&light,20,23,9,8);}
    else if(kind==1) {g.FillRectangle(&fill,3,12,31,26);g.DrawRectangle(&ink,3,12,31,26);
        Gdiplus::Point triangle[]={{36,19},{45,13},{45,37},{36,31}};g.FillPolygon(&fill,triangle,4);g.DrawRectangle(&light,8,17,20,16);}
    else if(kind==2) {g.DrawEllipse(&ink,5,5,38,38);g.DrawLine(&ink,24,12,24,25);g.DrawLine(&ink,24,25,34,30);}
    else if(kind==3) {for(int i=0;i<8;++i){auto s=g.Save();g.TranslateTransform(24.f,24.f);g.RotateTransform(i*45.f);g.FillRectangle(&fill,-4,-23,8,12);g.Restore(s);}
        g.FillEllipse(&fill,8,8,32,32);Gdiplus::SolidBrush hole(Gdiplus::Color(255,246,246,234));g.FillEllipse(&hole,17,17,14,14);}
    else if(kind==4) {g.FillEllipse(&fill,3,13,42,26);g.DrawLine(&light,11,26,23,26);g.DrawLine(&light,17,20,17,32);g.DrawEllipse(&light,32,20,3,3);g.DrawEllipse(&light,37,27,3,3);}
    else if(kind==5) {for(int i=0;i<4;++i){int hh=10+i*9;g.FillRectangle(&fill,4+i*11,43-hh,6,hh);}}
    else {g.DrawRectangle(&ink,10,10,28,28);g.DrawLine(&ink,4,10,44,10);g.DrawLine(&ink,10,4,10,44);g.DrawLine(&ink,17,32,33,17);}
    g.Restore(save);
}
}
