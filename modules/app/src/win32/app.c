#include <app/app.h>
#include <app/subsystem.h>
#include <reactor/reactor.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static bool app_init(Mel_Reactor* reactor, void* user)
{
    (void)user;
    mel_app_init(.reactor = reactor);
    mel_app_setup(reactor);
    return true;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    int rc = mel_reactor_spawn(MEL_REACTOR_THREADED, app_init, NULL);
    mel_app_quit();
    return rc;
}
