#include <gui.control/slider.h>
#include <gui.platform.win32/gui.platform.win32.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>

static HWND mel__slider_construct(Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc, HWND parent_hwnd)
{
    (void)h;
    HWND hwnd = CreateWindowExW(
        0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS,
        0, 0, desc->w, desc->h,
        parent_hwnd, (HMENU)(UINT_PTR)desc->id,
        mel_gui_win32_hinstance(), NULL);
    if (hwnd != NULL) {
        SendMessageW(hwnd, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        SendMessageW(hwnd, TBM_SETPOS,   TRUE, 0);
    }
    return hwnd;
}

void mel_gui_slider_platform_register(Mel_Atom atom)
{
    mel_gui_win32_register_constructor(atom, mel__slider_construct);
}
