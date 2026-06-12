#include <tts/provider.h>

#include <core/platform.h>

#if !MEL_PLATFORM_EMSCRIPTEN
#error "tts_web.c is for the emscripten runtime only"
#endif

#include <allocator/allocator.h>
#include <collection/array.h>
#include <string/str8.h>
#include <log/log.h>

#include <string.h>

#include <emscripten.h>

#define MEL_TTS_WEB_DONE_OK      0
#define MEL_TTS_WEB_DONE_ABORTED 1
#define MEL_TTS_WEB_DONE_ERROR   2
#define MEL_TTS_WEB_DONE_AUDIO   3
#define MEL_TTS_WEB_DONE_NETWORK 4

typedef struct
{
    str8 id;
    str8 name;
    str8 lang;
} Web_Voice;

typedef struct
{
    u64          token;
    Mel_Tts_Sink sink;
} Web_Job;

typedef Mel_Array(Web_Job) Web_Job_List;

static struct
{
    bool             registered;
    const Mel_Alloc* alloc;
    Mel_Array(Web_Voice) voices;
    Web_Job_List jobs;
    bool pause_noted;
} g_tts;

EM_JS(void, tts_web__js_init, (void), {
    if (globalThis.MelTts)
        return;
    var S = { snap : [], utts : {} };
    globalThis.MelTts = S;
    if (typeof speechSynthesis == 'undefined')
    {
        S.pull = function() { S.snap = []; };
        return;
    }
    S.pull = function() { S.snap = speechSynthesis.getVoices() || []; };
    S.changed = function()
    {
        S.pull();
        _mel_tts_web__on_voices_changed();
    };
    speechSynthesis.addEventListener('voiceschanged', S.changed);
    S.pull();
});

EM_JS(void, tts_web__js_shutdown, (void), {
    var S = globalThis.MelTts;
    if (!S)
        return;
    if (typeof speechSynthesis != 'undefined' && S.changed)
        speechSynthesis.removeEventListener('voiceschanged', S.changed);
    globalThis.MelTts = undefined;
});

EM_JS(int, tts_web__js_snap, (void), {
    var S = globalThis.MelTts;
    if (!S)
        return 0;
    S.pull();
    return S.snap.length;
});

EM_JS(int, tts_web__js_voice_field_len, (int idx, int field), {
    var S = globalThis.MelTts;
    var v = S && S.snap[idx];
    if (!v)
        return 0;
    var t = field == 0 ? (v.voiceURI || v.name || "") : field == 1 ? (v.name || "") : (v.lang || "");
    return lengthBytesUTF8(t);
});

EM_JS(void, tts_web__js_voice_field, (int idx, int field, char* buf, int cap), {
    var S = globalThis.MelTts;
    var v = S && S.snap[idx];
    var t = "";
    if (v)
        t = field == 0 ? (v.voiceURI || v.name || "") : field == 1 ? (v.name || "") : (v.lang || "");
    stringToUTF8(t, buf, cap);
});

EM_JS(int, tts_web__js_speak, (double lo, double hi, const char* text, int text_len, const char* uri, int uri_len, double rate, double pitch, double volume, int want_ranges), {
    var S = globalThis.MelTts;
    if (!S || typeof speechSynthesis == 'undefined')
        return 0;
    var t = UTF8ToString(text, text_len);
    var u = new SpeechSynthesisUtterance(t);
    var id = UTF8ToString(uri, uri_len);
    for (var i = 0; i < S.snap.length; i++)
    {
        var v = S.snap[i];
        if ((v.voiceURI || v.name || "") == id)
        {
            u.voice = v;
            break;
        }
    }
    if (rate > 0)
        u.rate = rate;
    if (pitch > 0)
        u.pitch = Math.min(2, pitch);
    if (volume > 0)
        u.volume = Math.min(1, volume);
    var key = lo + ':' + hi;
    u.onend = function()
    {
        delete S.utts[key];
        _mel_tts_web__on_done(lo, hi, 0);
    };
    u.onerror = function(e)
    {
        delete S.utts[key];
        var err = e && e.error ? e.error : "";
        var code = 2;
        if (err == 'canceled' || err == 'interrupted')
            code = 1;
        else if (err == 'audio-busy' || err == 'audio-hardware')
            code = 3;
        else if (err == 'network')
            code = 4;
        _mel_tts_web__on_done(lo, hi, code);
    };
    if (want_ranges)
    {
        u.onboundary = function(e)
        {
            var idx = e.charIndex || 0;
            var len = e.charLength;
            if (typeof len == 'undefined' || len <= 0)
            {
                var endi = idx;
                while (endi < t.length && t.charCodeAt(endi) > 32)
                    endi++;
                len = endi - idx;
            }
            var off8 = lengthBytesUTF8(t.substring(0, idx));
            var len8 = lengthBytesUTF8(t.substring(idx, idx + len));
            _mel_tts_web__on_range(lo, hi, off8, len8);
        };
    }
    S.utts[key] = u;
    speechSynthesis.speak(u);
    return 1;
});

EM_JS(void, tts_web__js_pause, (void), {
    if (typeof speechSynthesis != 'undefined')
        speechSynthesis.pause();
});

EM_JS(void, tts_web__js_resume, (void), {
    if (typeof speechSynthesis != 'undefined')
        speechSynthesis.resume();
});

EM_JS(void, tts_web__js_cancel_all, (void), {
    var S = globalThis.MelTts;
    if (S)
    {
        for (var k in S.utts)
        {
            var u = S.utts[k];
            u.onend = null;
            u.onerror = null;
            u.onboundary = null;
        }
        S.utts = {};
    }
    if (typeof speechSynthesis != 'undefined')
        speechSynthesis.cancel();
});

static Web_Job* job_find(u64 token)
{
    for (usize i = 0; i < g_tts.jobs.count; i++)
        if (g_tts.jobs.items[i].token == token)
            return &g_tts.jobs.items[i];
    return NULL;
}

static void job_remove(u64 token)
{
    for (usize i = 0; i < g_tts.jobs.count; i++)
    {
        if (g_tts.jobs.items[i].token == token)
        {
            mel_array_remove_unordered(&g_tts.jobs, i);
            return;
        }
    }
}

static Mel_Tts_Status done_status(int code)
{
    switch (code)
    {
    case MEL_TTS_WEB_DONE_OK:
        return MEL_TTS_OK;
    case MEL_TTS_WEB_DONE_ABORTED:
        return MEL_TTS_OK | MEL_TTS_RESULT_ABORTED;
    case MEL_TTS_WEB_DONE_AUDIO:
        return MEL_TTS_ERROR | MEL_TTS_RESULT_AUDIO;
    case MEL_TTS_WEB_DONE_NETWORK:
        return MEL_TTS_ERROR | MEL_TTS_RESULT_NETWORK;
    default:
        return MEL_TTS_ERROR;
    }
}

EMSCRIPTEN_KEEPALIVE void mel_tts_web__on_voices_changed(void)
{
    if (!g_tts.registered)
        return;
    mel_log_info("tts", "web: voiceschanged fired; re-enumerating");
    mel_tts_refresh();
}

EMSCRIPTEN_KEEPALIVE void mel_tts_web__on_done(unsigned lo, unsigned hi, int code)
{
    u64      token = ((u64)hi << 32) | (u64)lo;
    Web_Job* job = job_find(token);
    if (!job)
        return;
    Mel_Tts_Sink sink = job->sink;
    job_remove(token);
    if (sink.on_done)
        sink.on_done(sink.token, done_status(code));
}

EMSCRIPTEN_KEEPALIVE void mel_tts_web__on_range(unsigned lo, unsigned hi, int offset, int length)
{
    u64      token = ((u64)hi << 32) | (u64)lo;
    Web_Job* job = job_find(token);
    if (job && job->sink.on_range)
        job->sink.on_range(job->sink.token, (Mel_Tts_Range){ .offset = (usize)offset, .length = (usize)length });
}

static void web_ensure(const Mel_Alloc* alloc)
{
    if (g_tts.alloc != NULL)
        return;
    g_tts.alloc = alloc;
    mel_array_init(&g_tts.voices, alloc);
    mel_array_init(&g_tts.jobs, alloc);
}

static str8 voice_field_intern(int idx, int field)
{
    int len = tts_web__js_voice_field_len(idx, field);
    u8* data = (u8*)mel_alloc(g_tts.alloc, (usize)len + 1);
    if (!data)
        return STR8_EMPTY;
    tts_web__js_voice_field(idx, field, (char*)data, len + 1);
    return (str8){ data, (size)len };
}

static void cache_clear(void)
{
    for (usize i = 0; i < g_tts.voices.count; i++)
    {
        Web_Voice* v = &g_tts.voices.items[i];
        if (v->id.data)
            mel_dealloc(g_tts.alloc, v->id.data);
        if (v->name.data)
            mel_dealloc(g_tts.alloc, v->name.data);
        if (v->lang.data)
            mel_dealloc(g_tts.alloc, v->lang.data);
    }
    mel_array_clear(&g_tts.voices);
}

static Web_Voice* voice_by_id(u64 stable_id)
{
    for (usize i = 0; i < g_tts.voices.count; i++)
        if (str8_hash(g_tts.voices.items[i].id) == stable_id)
            return &g_tts.voices.items[i];
    return NULL;
}

static u32 web_enumerate_voices(void* user, const Mel_Alloc* alloc, Mel_Tts_Voice_Raw* out, u32 cap)
{
    MEL_UNUSED(user);
    web_ensure(alloc);
    cache_clear();
    u32 total = (u32)tts_web__js_snap();
    for (u32 i = 0; i < total; i++)
    {
        Web_Voice v = {
            .id = voice_field_intern((int)i, 0),
            .name = voice_field_intern((int)i, 1),
            .lang = voice_field_intern((int)i, 2),
        };
        mel_array_push(&g_tts.voices, v);
    }
    u32 n = total < cap ? total : cap;
    for (u32 i = 0; i < n; i++)
    {
        Web_Voice* v = &g_tts.voices.items[i];
        out[i] = (Mel_Tts_Voice_Raw){
            .stable_id = str8_hash(v->id),
            .name = v->name,
            .language = v->lang,
            .viseme_set = STR8_EMPTY,
            .caps = {
                .rate = true,
                .rate_min = 0.1f,
                .rate_max = 10.0f,
                .pitch = true,
                .volume = true,
                .ranges = true,
                .can_pause = true,
                .render = false,
                .ssml = false,
                .visemes = false,
            },
        };
    }
    return total;
}

static Mel_Tts_Status web_speak(void* user, u64 stable_id, u64 token, const Mel_Tts_Lowered* lowered, Mel_Tts_Sink sink)
{
    MEL_UNUSED(user);
    Web_Voice* v = voice_by_id(stable_id);
    if (!v)
    {
        mel_log_error("tts", "web speak: voice %llu not in cache", (unsigned long long)stable_id);
        return MEL_TTS_ERROR | MEL_TTS_RESULT_LOST;
    }
    Web_Job job = { .token = token, .sink = sink };
    mel_array_push(&g_tts.jobs, job);
    int ok = tts_web__js_speak((double)(u32)token,
                               (double)(u32)(token >> 32),
                               (const char*)lowered->text.data,
                               (int)lowered->text.len,
                               (const char*)v->id.data,
                               (int)v->id.len,
                               lowered->rate,
                               lowered->pitch,
                               lowered->volume,
                               lowered->want_ranges ? 1 : 0);
    if (!ok)
    {
        job_remove(token);
        mel_log_error("tts", "web speak: speechSynthesis unavailable");
        return MEL_TTS_ERROR | MEL_TTS_RESULT_UNSUPPORTED;
    }
    return MEL_TTS_OK;
}

static void web_pause(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    if (!g_tts.pause_noted)
    {
        g_tts.pause_noted = true;
        mel_log_warn("tts", "web: speechSynthesis pause/resume is global; every live utterance pauses and resumes together");
    }
    tts_web__js_pause();
}

static void web_resume(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    tts_web__js_resume();
}

static void web_abort(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    tts_web__js_cancel_all();
    if (g_tts.jobs.count > 1)
        mel_log_warn("tts", "web: speechSynthesis.cancel purges the whole queue; resolving %u other live utterance(s) aborted", (u32)(g_tts.jobs.count - 1));
    Web_Job_List snap = g_tts.jobs;
    mel_array_init(&g_tts.jobs, g_tts.alloc);
    for (usize i = 0; i < snap.count; i++)
    {
        Web_Job* j = &snap.items[i];
        if (j->token != token && j->sink.on_done)
            j->sink.on_done(j->sink.token, MEL_TTS_OK | MEL_TTS_RESULT_ABORTED);
    }
    mel_array_free(&snap);
}

static void web_shutdown(void* user, const Mel_Alloc* alloc)
{
    MEL_UNUSED(user);
    MEL_UNUSED(alloc);
    if (!g_tts.registered)
        return;
    tts_web__js_cancel_all();
    tts_web__js_shutdown();
    if (g_tts.jobs.count > 0)
        mel_log_warn("tts", "web: %u utterance job(s) unresolved at shutdown", (u32)g_tts.jobs.count);
    if (g_tts.alloc)
    {
        cache_clear();
        mel_array_free(&g_tts.voices);
        mel_array_free(&g_tts.jobs);
    }
    memset(&g_tts, 0, sizeof g_tts);
}

void mel_tts__register_host_providers(void)
{
    static const Mel_Tts_Provider_Desc desc = {
        .name = "web-speechsynthesis",
        .enumerate_voices = web_enumerate_voices,
        .speak = web_speak,
        .pause = web_pause,
        .resume = web_resume,
        .abort = web_abort,
        .shutdown = web_shutdown,
    };
    mel_tts_provider_register(&desc);
    g_tts.registered = true;
    tts_web__js_init();
}
