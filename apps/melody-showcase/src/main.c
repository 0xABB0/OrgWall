#include "showcase.h"

#include <string.h>

#include <app/subsystem.h>
#include <reactor/reactor.h>

static bool window_init(Mel_Reactor* reactor, void* user)
{
    (void)user;
    mel_app_init(.reactor = reactor);
    showcase_window_setup(reactor);
    return true;
}

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--smoke") == 0)
            return showcase_smoke();

    int rc = mel_reactor_spawn(MEL_REACTOR_THREADED, window_init, NULL);
    mel_app_quit();
    return rc;
}
