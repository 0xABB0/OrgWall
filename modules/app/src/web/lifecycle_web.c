#include <core/platform.h>

#if !MEL_PLATFORM_EMSCRIPTEN
#error "web/wasm-only translation unit"
#endif

#include <emscripten/html5.h>

#include <app/provider.h>

static bool g_installed;

static EM_BOOL on_visibility(int type, const EmscriptenVisibilityChangeEvent* ev, void* user)
{
    (void)type;
    (void)user;
    if (ev->hidden)
    {
        mel_app__emit(MEL_APP_PHASE_WILL_RESIGN_ACTIVE);
        mel_app__emit(MEL_APP_PHASE_DID_ENTER_BACKGROUND);
    }
    else
    {
        mel_app__emit(MEL_APP_PHASE_WILL_ENTER_FOREGROUND);
        mel_app__emit(MEL_APP_PHASE_DID_BECOME_ACTIVE);
    }
    return EM_TRUE;
}

static EM_BOOL on_beforeunload(int type, const void* reserved, void* user)
{
    (void)type;
    (void)reserved;
    (void)user;
    mel_app__emit(MEL_APP_PHASE_WILL_TERMINATE);
    return EM_FALSE;
}

static void plat_start(void* user)
{
    (void)user;
    if (g_installed)
        return;
    emscripten_set_visibilitychange_callback(NULL, EM_FALSE, on_visibility);
    emscripten_set_beforeunload_callback(NULL, on_beforeunload);
    g_installed = true;
}

static void plat_stop(void* user)
{
    (void)user;
    if (!g_installed)
        return;
    emscripten_set_visibilitychange_callback(NULL, EM_FALSE, NULL);
    emscripten_set_beforeunload_callback(NULL, NULL);
    g_installed = false;
}

void mel_app__register_platform_provider(void)
{
    Mel_App_Provider_Desc desc = { .name = "web-visibility", .start = plat_start, .stop = plat_stop };
    mel_app_provider_register(&desc);
}
