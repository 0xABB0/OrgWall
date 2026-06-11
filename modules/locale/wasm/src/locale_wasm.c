#include <core/platform.h>

#if !MEL_PLATFORM_EMSCRIPTEN
#error "wasm/emscripten-only translation unit"
#endif

#include <locale/provider.h>
#include <log/log.h>

#include <allocator/allocator.h>

#include <emscripten.h>

#include <string.h>

#define MEL_LOCALE_WASM_TAG_CAP 48

static Mel_Locale_Change_Notify g_notify;
static void*                    g_core;

EM_JS(int, mel_locale_wasm__count, (void), {
    if (navigator.languages && navigator.languages.length)
        return navigator.languages.length;
    if (navigator.language)
        return 1;
    return 0;
});

EM_JS(void, mel_locale_wasm__tag, (int idx, char* buf, int cap), {
    var list = (navigator.languages && navigator.languages.length) ? navigator.languages : (navigator.language ? [ navigator.language ] : []);
    var t = list[idx] || "";
    stringToUTF8(t, buf, cap);
});

EM_JS(void, mel_locale_wasm__watch, (void), {
    if (Module._melLocaleListener)
        return;
    Module._melLocaleListener = function() { Module._mel_locale_wasm__on_change(); };
    window.addEventListener('languagechange', Module._melLocaleListener);
});

EM_JS(void, mel_locale_wasm__unwatch, (void), {
    if (Module._melLocaleListener)
    {
        window.removeEventListener('languagechange', Module._melLocaleListener);
        Module._melLocaleListener = null;
    }
});

EMSCRIPTEN_KEEPALIVE void mel_locale_wasm__on_change(void)
{
    if (g_notify)
        g_notify(g_core);
}

static u32 wasm_enumerate(void* user, const Mel_Alloc* alloc, Mel_Locale_Raw* out, u32 cap)
{
    (void)user;
    u32 count = (u32)mel_locale_wasm__count();
    if (count > cap)
        return count;
    u32  produced = 0;
    char tmp[MEL_LOCALE_WASM_TAG_CAP];
    for (u32 i = 0; i < count; i++)
    {
        tmp[0] = '\0';
        mel_locale_wasm__tag((int)i, tmp, (int)sizeof tmp);
        usize len = strlen(tmp);
        if (len == 0)
            continue;
        u8* buf = (u8*)mel_alloc(alloc, len);
        if (!buf)
            continue;
        memcpy(buf, tmp, len);
        out[produced++] = (Mel_Locale_Raw){ .tag = { .data = buf, .len = (size)len } };
    }
    return produced;
}

static void wasm_watch(void* user, Mel_Locale_Change_Notify notify, void* core)
{
    (void)user;
    g_notify = notify;
    g_core = core;
    mel_locale_wasm__watch();
}

static void wasm_unwatch(void* user)
{
    (void)user;
    mel_locale_wasm__unwatch();
    g_notify = NULL;
    g_core = NULL;
}

void mel_locale__register_host_providers(void)
{
    static const Mel_Locale_Provider_Desc desc = {
        .name = "wasm-navigator",
        .enumerate = wasm_enumerate,
        .watch = wasm_watch,
        .unwatch = wasm_unwatch,
    };
    mel_locale_provider_register(&desc);
}
