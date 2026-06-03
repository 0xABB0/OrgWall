#include <core/platform.h>

#if !MEL_PLATFORM_EMSCRIPTEN
#error "web/wasm-only translation unit"
#endif

#include <emscripten/console.h>

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
    (void)argc;
    (void)argv;

    int rc = mel_reactor_spawn(MEL_REACTOR_ATTACHED, app_init, NULL);
    if (rc != 0)
        emscripten_console_error("mel app: reactor spawn failed on web; GUI/GPU host never started");
    return rc;
}
