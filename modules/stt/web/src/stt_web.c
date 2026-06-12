#include <stt/provider.h>

#include <core/platform.h>

#if !MEL_PLATFORM_EMSCRIPTEN
#error "stt_web.c is for the emscripten runtime only"
#endif

#include <allocator/allocator.h>
#include <string/str8.h>
#include <log/log.h>

#include <emscripten.h>
#include <emscripten/em_js.h>

#include <string.h>

#define MEL_STT_WEB_DONE_OK        0
#define MEL_STT_WEB_DONE_ABORTED   1
#define MEL_STT_WEB_DONE_ERROR     2
#define MEL_STT_WEB_DONE_DENIED    3
#define MEL_STT_WEB_DONE_AUDIO     4
#define MEL_STT_WEB_DONE_NETWORK   5
#define MEL_STT_WEB_DONE_NO_SPEECH 6

typedef struct
{
    u64          token;
    Mel_Stt_Sink sink;
    bool         live;
} Web_Session;

static struct
{
    const Mel_Alloc* alloc;
    str8             id;
    str8             lang;
    u64              stable_id;
    u8*              text_buf;
    usize            text_cap;
    Web_Session      session;
} g_stt;

EM_JS(void, stt_web__js_init, (void), {
    if (globalThis.MelStt)
        return;
    globalThis.MelStt = { recs : {} };
});

EM_JS(int, stt_web__js_supported, (void), {
    var Ctor = globalThis.SpeechRecognition || globalThis.webkitSpeechRecognition;
    if (!Ctor)
        return 0;
    if (typeof navigator == 'undefined' || !navigator.language)
        return 0;
    return 1;
});

EM_JS(int, stt_web__js_lang_len, (void), {
    var l = (typeof navigator != 'undefined' && navigator.language) ? navigator.language : "";
    return lengthBytesUTF8(l);
});

EM_JS(void, stt_web__js_lang, (char* buf, int cap), {
    var l = (typeof navigator != 'undefined' && navigator.language) ? navigator.language : "";
    stringToUTF8(l, buf, cap);
});

EM_JS(int, stt_web__js_listen, (double lo, double hi, const char* lang, int lang_len, int partials), {
    var S = globalThis.MelStt;
    var Ctor = globalThis.SpeechRecognition || globalThis.webkitSpeechRecognition;
    if (!S || !Ctor)
        return 0;
    var key = lo + ':' + hi;
    var r = new Ctor();
    r.lang = UTF8ToString(lang, lang_len);
    r.continuous = true;
    r.interimResults = partials ? true : false;
    r.onresult = function(e)
    {
        for (var i = e.resultIndex; i < e.results.length; i++)
        {
            var res = e.results[i];
            var alt = res[0];
            if (!alt)
                continue;
            var t = alt.transcript || "";
            var n = lengthBytesUTF8(t) + 1;
            var ptr = _mel_stt_web__text_buf(n);
            if (!ptr)
                continue;
            stringToUTF8(t, ptr, n);
            _mel_stt_web__on_result(lo, hi, ptr, n - 1, res.isFinal ? 1 : 0, alt.confidence || 0);
        }
    };
    r.onerror = function(e)
    {
        var code = 2;
        if (e.error == 'not-allowed' || e.error == 'service-not-allowed')
            code = 3;
        else if (e.error == 'audio-capture')
            code = 4;
        else if (e.error == 'network')
            code = 5;
        else if (e.error == 'no-speech')
            code = 6;
        else if (e.error == 'aborted')
            code = 1;
        r.melCode = code;
    };
    r.onend = function()
    {
        delete S.recs[key];
        _mel_stt_web__on_done(lo, hi, typeof r.melCode == 'undefined' ? 0 : r.melCode);
    };
    S.recs[key] = r;
    try
    {
        r.start();
    }
    catch (err)
    {
        delete S.recs[key];
        return 0;
    }
    return 1;
});

EM_JS(void, stt_web__js_stop, (double lo, double hi), {
    var S = globalThis.MelStt;
    var r = S && S.recs[lo + ':' + hi];
    if (r)
        r.stop();
});

EM_JS(void, stt_web__js_abort, (double lo, double hi), {
    var S = globalThis.MelStt;
    var r = S && S.recs[lo + ':' + hi];
    if (r)
    {
        delete S.recs[lo + ':' + hi];
        r.onresult = null;
        r.onerror = null;
        r.onend = null;
        r.abort();
    }
});

EM_JS(void, stt_web__js_shutdown, (void), {
    var S = globalThis.MelStt;
    if (!S)
        return;
    for (var key in S.recs)
    {
        var r = S.recs[key];
        r.onresult = null;
        r.onerror = null;
        r.onend = null;
        r.abort();
    }
    globalThis.MelStt = undefined;
});

static Mel_Stt_Status web_done_status(int code)
{
    switch (code)
    {
    case MEL_STT_WEB_DONE_OK:
        return MEL_STT_OK;
    case MEL_STT_WEB_DONE_ABORTED:
        return MEL_STT_OK | MEL_STT_RESULT_ABORTED;
    case MEL_STT_WEB_DONE_DENIED:
        return MEL_STT_ERROR | MEL_STT_RESULT_DENIED;
    case MEL_STT_WEB_DONE_AUDIO:
        return MEL_STT_ERROR | MEL_STT_RESULT_AUDIO;
    case MEL_STT_WEB_DONE_NETWORK:
        return MEL_STT_ERROR | MEL_STT_RESULT_NETWORK;
    case MEL_STT_WEB_DONE_NO_SPEECH:
        return MEL_STT_OK;
    default:
        return MEL_STT_ERROR;
    }
}

EMSCRIPTEN_KEEPALIVE u8* mel_stt_web__text_buf(int bytes)
{
    if (bytes <= 0 || g_stt.alloc == NULL)
        return NULL;
    if ((usize)bytes > g_stt.text_cap)
    {
        if (g_stt.text_buf)
            mel_dealloc(g_stt.alloc, g_stt.text_buf);
        g_stt.text_buf = (u8*)mel_alloc(g_stt.alloc, (usize)bytes);
        g_stt.text_cap = g_stt.text_buf ? (usize)bytes : 0;
    }
    return g_stt.text_buf;
}

EMSCRIPTEN_KEEPALIVE void mel_stt_web__on_result(unsigned lo, unsigned hi, const char* text, int len, int final, float confidence)
{
    u64 token = ((u64)hi << 32) | (u64)lo;
    if (!g_stt.session.live || g_stt.session.token != token || g_stt.session.sink.on_result == NULL)
        return;
    Mel_Stt_Result res = {
        .text = (str8){ (u8*)text, (size)len },
        .final = final != 0,
        .confidence = confidence,
    };
    g_stt.session.sink.on_result(g_stt.session.sink.token, &res);
}

EMSCRIPTEN_KEEPALIVE void mel_stt_web__on_done(unsigned lo, unsigned hi, int code)
{
    u64 token = ((u64)hi << 32) | (u64)lo;
    if (!g_stt.session.live || g_stt.session.token != token)
        return;
    Mel_Stt_Sink sink = g_stt.session.sink;
    g_stt.session.live = false;
    if (code == MEL_STT_WEB_DONE_NO_SPEECH)
        mel_log_info("stt", "web: recognition ended with no speech; resolving OK with no transcript");
    if (sink.on_done)
        sink.on_done(sink.token, web_done_status(code));
}

static void web_strings_free(void)
{
    if (g_stt.id.data)
        mel_dealloc(g_stt.alloc, g_stt.id.data);
    if (g_stt.lang.data)
        mel_dealloc(g_stt.alloc, g_stt.lang.data);
    g_stt.id = STR8_EMPTY;
    g_stt.lang = STR8_EMPTY;
    g_stt.stable_id = 0;
}

static u32 web_enumerate_recognizers(void* user, const Mel_Alloc* alloc, Mel_Stt_Recognizer_Raw* out, u32 cap)
{
    MEL_UNUSED(user);
    if (g_stt.alloc == NULL)
    {
        g_stt.alloc = alloc;
        stt_web__js_init();
    }
    if (!stt_web__js_supported())
    {
        mel_log_warn("stt", "web: SpeechRecognition unavailable; no recognizers");
        return 0;
    }
    if (cap >= 1)
    {
        web_strings_free();
        int lang_len = stt_web__js_lang_len();
        u8* lang_data = (u8*)mel_alloc(g_stt.alloc, (usize)lang_len + 1);
        if (!lang_data)
            return 0;
        stt_web__js_lang((char*)lang_data, lang_len + 1);
        g_stt.lang = (str8){ lang_data, (size)lang_len };
        g_stt.id = str8_fmt(g_stt.alloc, "web:%.*s", lang_len, (const char*)lang_data);
        if (!g_stt.id.data)
        {
            web_strings_free();
            return 0;
        }
        g_stt.stable_id = str8_hash(g_stt.id);
        out[0] = (Mel_Stt_Recognizer_Raw){
            .stable_id = g_stt.stable_id,
            .language = g_stt.lang,
            .caps = {
                .on_device = false,
                .require_on_device = false,
                .partials = true,
                .can_stop = true,
                .feed = false,
                .device_select = false,
                .vocabulary = false,
                .punctuation = false,
                .profanity_filter = false,
            },
        };
    }
    return 1;
}

static const mel_stt_auth* web_authorization(void* user)
{
    MEL_UNUSED(user);
    return stt_web__js_supported() ? &mel_stt_auth_granted : &mel_stt_auth_restricted;
}

static void web_authorize(void* user, Mel_Stt_Sink sink)
{
    if (sink.on_auth)
        sink.on_auth(sink.token, web_authorization(user));
}

static Mel_Stt_Status web_listen(void* user, u64 stable_id, u64 token, const Mel_Stt_Listen_Lowered* lowered, Mel_Stt_Sink sink)
{
    MEL_UNUSED(user);
    if (g_stt.lang.len == 0 || stable_id != g_stt.stable_id)
    {
        mel_log_error("stt", "web listen: recognizer %llu not found", (unsigned long long)stable_id);
        return MEL_STT_ERROR | MEL_STT_RESULT_NO_DEVICE;
    }
    if (g_stt.session.live)
    {
        mel_log_error("stt", "web listen: a session is already live; provider bug upstream");
        return MEL_STT_ERROR | MEL_STT_RESULT_BUSY;
    }
    g_stt.session = (Web_Session){ .token = token, .sink = sink, .live = true };
    int ok = stt_web__js_listen((double)(u32)token, (double)(u32)(token >> 32), (const char*)g_stt.lang.data, (int)g_stt.lang.len, lowered->partials ? 1 : 0);
    if (!ok)
    {
        g_stt.session.live = false;
        mel_log_error("stt", "web listen: SpeechRecognition failed to start");
        return MEL_STT_ERROR | MEL_STT_RESULT_UNSUPPORTED;
    }
    return MEL_STT_OK;
}

static void web_stop(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    if (!g_stt.session.live || g_stt.session.token != token)
        return;
    stt_web__js_stop((double)(u32)token, (double)(u32)(token >> 32));
}

static void web_abort(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    if (!g_stt.session.live || g_stt.session.token != token)
        return;
    g_stt.session.live = false;
    stt_web__js_abort((double)(u32)token, (double)(u32)(token >> 32));
}

static void web_shutdown(void* user, const Mel_Alloc* alloc)
{
    MEL_UNUSED(user);
    MEL_UNUSED(alloc);
    if (g_stt.alloc == NULL)
        return;
    if (g_stt.session.live)
    {
        u64 token = g_stt.session.token;
        g_stt.session.live = false;
        stt_web__js_abort((double)(u32)token, (double)(u32)(token >> 32));
    }
    stt_web__js_shutdown();
    web_strings_free();
    if (g_stt.text_buf)
        mel_dealloc(g_stt.alloc, g_stt.text_buf);
    memset(&g_stt, 0, sizeof g_stt);
}

void mel_stt__register_host_providers(void)
{
    static const Mel_Stt_Provider_Desc desc = {
        .name = "web-speechrecognition",
        .enumerate_recognizers = web_enumerate_recognizers,
        .authorization = web_authorization,
        .authorize = web_authorize,
        .listen = web_listen,
        .stop = web_stop,
        .abort = web_abort,
        .feed = NULL,
        .recognizer_native = NULL,
        .shutdown = web_shutdown,
    };
    mel_stt_provider_register(&desc);
}
