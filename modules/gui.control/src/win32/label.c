#include <gui.control/label.h>
#include <gui.platform.win32/gui.platform.win32.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static HWND mel__label_construct(Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc, HWND parent_hwnd)
{
    (void)h;
    wchar_t* text = mel_gui_win32_str8_to_wide(desc->text);
    HWND hwnd = CreateWindowExW(
        0, L"STATIC", text ? text : L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, desc->w, desc->h,
        parent_hwnd, (HMENU)(UINT_PTR)desc->id,
        mel_gui_win32_hinstance(), NULL);
    mel_gui_win32_wide_free(text);
    return hwnd;
}

void mel_gui_label_platform_register(Mel_Atom atom)
{
    mel_gui_win32_register_constructor(atom, mel__label_construct);
}
