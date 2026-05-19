#include <app/app.h>
#include <reactor/reactor.h>

int main(int argc, char** argv)
{
    (void)argc; (void)argv;
    if (!mel_reactor_init()) return 1;
    mel_app_setup(mel_reactor_system());
    mel_reactor_run(mel_reactor_system());
    mel_reactor_shutdown();
    return 0;
}
