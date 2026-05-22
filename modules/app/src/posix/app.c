#include <app/app.h>
#include <reactor/reactor.h>

static bool app_init(Mel_Reactor* reactor, void* user)
{
    (void)user;
    mel_app_setup(reactor);
    return true;
}

int main(int argc, char** argv)
{
    (void)argc; (void)argv;
    return mel_reactor_spawn(MEL_REACTOR_THREADED, app_init, NULL);
}
