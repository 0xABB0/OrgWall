#include <gui.control/panel.h>
#include <gui.platform.win32/gui.platform.win32.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static HWND mel__panel_construct(Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc, HWND parent_hwnd)
{
    (void)h;
    HWND hwnd = CreateWindowExW(
        WS_EX_CONTROLPARENT, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | SS_OWNERDRAW,
        0, 0, desc->w, desc->h,
        parent_hwnd, (HMENU)(UINT_PTR)desc->id,
        mel_gui_win32_hinstance(), NULL);
    return hwnd;
}

void mel_gui_panel_platform_register(Mel_Atom atom)
{
    mel_gui_win32_register_constructor(atom, mel__panel_construct);
}
