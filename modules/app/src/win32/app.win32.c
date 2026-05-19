#include <app/app.h>
#include <reactor/reactor.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hInstance; (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;
    if (!mel_reactor_init()) return 1;
    mel_app_setup(mel_reactor_system());
    mel_reactor_run(mel_reactor_system());
    mel_reactor_shutdown();
    return 0;
}
