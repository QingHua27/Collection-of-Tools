#pragma once
#include <windows.h>
namespace toolbox::theme {
constexpr COLORREF Paper=RGB(247,246,236), Card=RGB(253,252,246);
constexpr COLORREF Ink=RGB(37,62,44), Green=RGB(67,103,63), Muted=RGB(111,123,101);
constexpr COLORREF Border=RGB(202,207,185), Selected=RGB(220,227,202);
void Apply(HWND root);
void Dialog(HWND window, HWND owner);
void PaintBackdrop(HDC dc, RECT bounds, bool foliage=true);
bool Hovered(HWND window);
void FocusRing(HDC dc,RECT bounds,float radius);
void Rounded(HDC dc,RECT bounds,COLORREF color,float radius);
void Icon(HDC dc, RECT bounds, int kind);
void PaintSurface(HWND window,HDC dc,RECT bounds);
unsigned BackgroundBuilds();
}
