#include <app/app.h>
#include <app/subsystem.h>
#include <reactor/reactor.h>

static bool app_init(Mel_Reactor* reactor, void* user)
{
    (void)user;
    mel_app_init(.reactor = reactor);
    mel_app_setup(reactor);
    return true;
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    int rc = mel_reactor_spawn(MEL_REACTOR_THREADED, app_init, NULL);
    mel_app_quit();
    return rc;
}
