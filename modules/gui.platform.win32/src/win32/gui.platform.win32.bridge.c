#include <gui.platform.win32/gui.platform.win32.h>
#include <gui.platform/gui.platform.h>
#include <gui/gui.h>
#include <string/string.str8.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static void mel__w_setup(void)
{
    if (!mel_gui_init()) return;
    mel_gui_app_build_activity(S8("main"));
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nShowCmd;
    mel_gui_win32_run(mel__w_setup);
    return 0;
}
