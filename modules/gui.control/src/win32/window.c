#include <gui.control/window.h>
#include <gui.platform.win32/gui.platform.win32.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static HWND mel__window_construct(Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc, HWND parent_hwnd)
{
    (void)h;
    (void)parent_hwnd;

    HWND root = mel_gui_win32_root();
    if (desc->text.len > 0) {
        wchar_t* w = mel_gui_win32_str8_to_wide(desc->text);
        if (w) {
            SetWindowTextW(root, w);
            mel_gui_win32_wide_free(w);
        }
    }
    return root;
}

void mel_gui_window_platform_register(Mel_Atom atom)
{
    mel_gui_win32_register_constructor(atom, mel__window_construct);
}
