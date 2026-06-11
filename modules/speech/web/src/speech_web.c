#include <speech/provider.h>

#include <allocator/allocator.h>
#include <collection/array.h>
#include <string/str8.h>
#include <log/log.h>

#include <string.h>

#include <emscripten.h>

#define MEL_SPEECH_WEB_RECOGNIZER_ID 0x77656273F3C1ull

#define MEL_SPEECH_WEB_DONE_OK       0
#define MEL_SPEECH_WEB_DONE_ABORTED  1
#define MEL_SPEECH_WEB_DONE_ERROR    2
#define MEL_SPEECH_WEB_DONE_DENIED   3
#define MEL_SPEECH_WEB_DONE_AUDIO    4
#define MEL_SPEECH_WEB_DONE_NETWORK  5

typedef struct
{
    u64             token;
    Mel_Speech_Sink sink;
} Web_Job;

typedef Mel_Array(Web_Job) Web_Jobs;
typedef Mel_Array(str8) Web_Strings;

typedef struct
{
    const Mel_Alloc* alloc;
    Web_Jobs         tts_jobs;
    Web_Jobs         stt_jobs;
    Web_Strings      voice_strings;
    Web_Strings      rec_strings;
    u8*              text_buf;
    usize            text_cap;
    Mel_Speech_Sink  auth_sink;
    bool             auth_pending;
} Web;

static Web g_web;

static str8 web_intern(Web_Strings* strings, const char* utf8)
{
    usize len = utf8 ? strlen(utf8) : 0;
    u8*   data = (u8*)mel_alloc(g_web.alloc, len + 1);
    if (!data)
        return (str8){ 0 };
    if (len)
        memcpy(data, utf8, len);
    data[len] = 0;
    str8 s = { data, (size)len };
    mel_array_push(strings, s);
    return s;
}

static void web_strings_clear(Web_Strings* strings)
{
    for (usize i = 0; i < strings->count; i++)
        if (strings->items[i].data)
            mel_dealloc(g_web.alloc, strings->items[i].data);
    mel_array_clear(strings);
}

static Web_Job* web_job_find(Web_Jobs* jobs, u64 token)
{
    for (usize i = 0; i < jobs->count; i++)
        if (jobs->items[i].token == token)
            return &jobs->items[i];
    return NULL;
}

static void web_job_remove(Web_Jobs* jobs, u64 token)
{
    for (usize i = 0; i < jobs->count; i++)
    {
        if (jobs->items[i].token == token)
        {
            jobs->items[i] = jobs->items[jobs->count - 1];
            jobs->count--;
            return;
        }
    }
}

static Mel_Speech_Status web_done_status(int code)
{
    switch (code)
    {
    case MEL_SPEECH_WEB_DONE_OK:
        return MEL_SPEECH_OK;
    case MEL_SPEECH_WEB_DONE_ABORTED:
        return MEL_SPEECH_OK | MEL_SPEECH_RESULT_ABORTED;
    case MEL_SPEECH_WEB_DONE_DENIED:
        return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_DENIED;
    case MEL_SPEECH_WEB_DONE_AUDIO:
        return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_AUDIO;
    case MEL_SPEECH_WEB_DONE_NETWORK:
        return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_NETWORK;
    default:
        return MEL_SPEECH_ERROR;
    }
}

EM_JS(int, mel_speech_web__voices_refresh, (void), {
    if (!window.speechSynthesis)
        return 0;
    Module._melSpeechVoices = window.speechSynthesis.getVoices() || [];
    if (!Module._melSpeechVoicesHooked)
    {
        Module._melSpeechVoicesHooked = true;
        window.speechSynthesis.onvoiceschanged = function() { Module._melSpeechVoices = window.speechSynthesis.getVoices() || []; };
    }
    return Module._melSpeechVoices.length;
});

EM_JS(void, mel_speech_web__voice_field, (int idx, int field, char* buf, int cap), {
    var v = (Module._melSpeechVoices || [])[idx];
    var t = "";
    if (v)
        t = field == 0 ? (v.voiceURI || v.name || "") : field == 1 ? (v.name || "") : (v.lang || "");
    stringToUTF8(t, buf, cap);
});

EM_JS(int, mel_speech_web__speak, (double lo, double hi, const char* text, int text_len, const char* voice_uri, double rate, double pitch, double volume, int want_ranges), {
    if (!window.speechSynthesis)
        return 0;
    var t = UTF8ToString(text, text_len);
    var u = new SpeechSynthesisUtterance(t);
    var         uri = UTF8ToString(voice_uri);
    var         voices = Module._melSpeechVoices || [];
    for (var i = 0; i < voices.length; i++)
    {
        if ((voices[i].voiceURI || voices[i].name) == = uri)
        {
            u.voice = voices[i];
            break;
        }
    }
    if (rate > 0)
        u.rate = rate;
    if (pitch > 0)
        u.pitch = pitch;
    if (volume > 0)
        u.volume = volume;
    u.onend = function()
    {
        delete(Module._melSpeechUtts || {})[lo + ':' + hi];
        _mel_speech_web__on_speak_done(lo, hi, 0);
    };
    u.onerror = function(e)
    {
        delete(Module._melSpeechUtts || {})[lo + ':' + hi];
        _mel_speech_web__on_speak_done(lo, hi, e.error == = 'canceled' || e.error == = 'interrupted' ? 1 : 2);
    };
    if (want_ranges)
    {
        u.onboundary = function(e)
        {
            var idx = e.charIndex || 0;
            var len = e.charLength;
            if (len == = undefined || len <= 0)
            {
                var endi = idx;
                while (endi < t.length && t.charCodeAt(endi) > 32)
                    endi++;
                len = endi - idx;
            }
            var off8 = lengthBytesUTF8(t.substring(0, idx));
            var len8 = lengthBytesUTF8(t.substring(idx, idx + len));
            _mel_speech_web__on_range(lo, hi, off8, len8);
        };
    }
    if (!Module._melSpeechUtts)
        Module._melSpeechUtts = {};
    Module._melSpeechUtts[lo + ':' + hi] = u;
    window.speechSynthesis.speak(u);
    return 1;
});

EM_JS(void, mel_speech_web__tts_pause, (void), {
    if (window.speechSynthesis)
        window.speechSynthesis.pause();
});

EM_JS(void, mel_speech_web__tts_resume, (void), {
    if (window.speechSynthesis)
        window.speechSynthesis.resume();
});

EM_JS(void, mel_speech_web__tts_cancel, (void), {
    if (window.speechSynthesis)
        window.speechSynthesis.cancel();
});

EM_JS(int, mel_speech_web__stt_supported, (void), { return (window.SpeechRecognition || window.webkitSpeechRecognition) ? 1 : 0; });

EM_JS(void, mel_speech_web__stt_lang, (char* buf, int cap), { stringToUTF8(navigator.language || "en-US", buf, cap); });

EM_JS(int, mel_speech_web__listen, (double lo, double hi, const char* lang, int partials), {
    var Ctor = window.SpeechRecognition || window.webkitSpeechRecognition;
    if (!Ctor)
        return 0;
    var r = new Ctor();
    r.lang = UTF8ToString(lang);
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
            var ptr = _mel_speech_web__text_buf(n);
            if (!ptr)
                continue;
            stringToUTF8(t, ptr, n);
            _mel_speech_web__on_result(lo, hi, ptr, n - 1, res.isFinal ? 1 : 0, alt.confidence || 0);
        }
    };
    r.onerror = function(e)
    {
        var code = 2;
        if (e.error == = 'not-allowed' || e.error == = 'service-not-allowed')
            code = 3;
        else if (e.error == = 'audio-capture' || e.error == = 'no-speech')
            code = 4;
        else if (e.error == = 'network')
            code = 5;
        else if (e.error == = 'aborted')
            code = 1;
        r._melErr = code;
    };
    r.onend = function()
    {
        delete(Module._melSpeechRecs || {})[lo + ':' + hi];
        _mel_speech_web__on_listen_done(lo, hi, r._melErr == = undefined ? 0 : r._melErr);
    };
    if (!Module._melSpeechRecs)
        Module._melSpeechRecs = {};
    Module._melSpeechRecs[lo + ':' + hi] = r;
    try { r.start(); }
    catch(err)
    {
        delete Module._melSpeechRecs[lo + ':' + hi];
        return 0;
    }
    return 1;
});

EM_JS(void, mel_speech_web__listen_stop, (double lo, double hi), {
    var r = (Module._melSpeechRecs || {})[lo + ':' + hi];
    if (r)
        r.stop();
});

EM_JS(void, mel_speech_web__listen_abort, (double lo, double hi), {
    var r = (Module._melSpeechRecs || {})[lo + ':' + hi];
    if (r)
    {
        delete Module._melSpeechRecs[lo + ':' + hi];
        r.onend = null;
        r.onresult = null;
        r.onerror = null;
        r.abort();
    }
});

EM_JS(int, mel_speech_web__auth_state, (void), {
    var s = Module._melSpeechAuth;
    return s == = 'granted' ? 1 : s == = 'denied' ? 2 : 0;
});

EM_JS(void, mel_speech_web__auth_probe, (void), {
    if (!navigator.permissions || !navigator.permissions.query)
        return;
    navigator.permissions.query({ name : 'microphone' })
        .then(function(st) {
            if (st.state == = 'granted' || st.state == = 'denied')
                Module._melSpeechAuth = st.state;
            st.onchange = function() { Module._melSpeechAuth = st.state == = 'prompt' ? undefined : st.state; };
        })
        .catch(function(){});
});

EM_JS(void, mel_speech_web__authorize, (void), {
    if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia)
    {
        Module._melSpeechAuth = 'denied';
        _mel_speech_web__on_auth(2);
        return;
    }
    navigator.mediaDevices.getUserMedia({ audio : true })
        .then(function(stream) {
            stream.getTracks().forEach(function(tr) { tr.stop(); });
            Module._melSpeechAuth = 'granted';
            _mel_speech_web__on_auth(1);
        })
        .catch(function() {
            Module._melSpeechAuth = 'denied';
            _mel_speech_web__on_auth(2);
        });
});

EMSCRIPTEN_KEEPALIVE u8* mel_speech_web__text_buf(int bytes)
{
    if (bytes <= 0 || g_web.alloc == NULL)
        return NULL;
    if ((usize)bytes > g_web.text_cap)
    {
        if (g_web.text_buf)
            mel_dealloc(g_web.alloc, g_web.text_buf);
        g_web.text_buf = (u8*)mel_alloc(g_web.alloc, (usize)bytes);
        g_web.text_cap = g_web.text_buf ? (usize)bytes : 0;
    }
    return g_web.text_buf;
}

EMSCRIPTEN_KEEPALIVE void mel_speech_web__on_speak_done(unsigned lo, unsigned hi, int code)
{
    u64      token = ((u64)hi << 32) | (u64)lo;
    Web_Job* job = web_job_find(&g_web.tts_jobs, token);
    if (!job)
        return;
    Mel_Speech_Sink sink = job->sink;
    web_job_remove(&g_web.tts_jobs, token);
    if (sink.on_speak_done)
        sink.on_speak_done(sink.token, web_done_status(code));
}

EMSCRIPTEN_KEEPALIVE void mel_speech_web__on_range(unsigned lo, unsigned hi, int offset, int length)
{
    u64      token = ((u64)hi << 32) | (u64)lo;
    Web_Job* job = web_job_find(&g_web.tts_jobs, token);
    if (job && job->sink.on_range)
        job->sink.on_range(job->sink.token, (Mel_Speech_Range){ .offset = (usize)offset, .length = (usize)length });
}

EMSCRIPTEN_KEEPALIVE void mel_speech_web__on_result(unsigned lo, unsigned hi, const char* text, int len, int final, float confidence)
{
    u64      token = ((u64)hi << 32) | (u64)lo;
    Web_Job* job = web_job_find(&g_web.stt_jobs, token);
    if (!job || !job->sink.on_result)
        return;
    Mel_Speech_Result res = {
        .text = (str8){ (u8*)text, (size)len },
        .final = final != 0,
        .confidence = confidence,
    };
    job->sink.on_result(job->sink.token, &res);
}

EMSCRIPTEN_KEEPALIVE void mel_speech_web__on_listen_done(unsigned lo, unsigned hi, int code)
{
    u64      token = ((u64)hi << 32) | (u64)lo;
    Web_Job* job = web_job_find(&g_web.stt_jobs, token);
    if (!job)
        return;
    Mel_Speech_Sink sink = job->sink;
    web_job_remove(&g_web.stt_jobs, token);
    if (sink.on_listen_done)
        sink.on_listen_done(sink.token, web_done_status(code));
}

EMSCRIPTEN_KEEPALIVE void mel_speech_web__on_auth(int code)
{
    if (!g_web.auth_pending)
        return;
    g_web.auth_pending = false;
    Mel_Speech_Sink sink = g_web.auth_sink;
    if (sink.on_auth)
        sink.on_auth(sink.token, code == 1 ? &mel_speech_auth_granted : &mel_speech_auth_denied);
}

static void web_ensure(const Mel_Alloc* alloc)
{
    if (g_web.alloc != NULL)
        return;
    g_web.alloc = alloc;
    mel_array_init(&g_web.tts_jobs, alloc);
    mel_array_init(&g_web.stt_jobs, alloc);
    mel_array_init(&g_web.voice_strings, alloc);
    mel_array_init(&g_web.rec_strings, alloc);
    mel_speech_web__auth_probe();
}

static u32 web_enumerate_voices(void* user, const Mel_Alloc* alloc, Mel_Speech_Voice_Raw* out, u32 cap)
{
    MEL_UNUSED(user);
    web_ensure(alloc);
    web_strings_clear(&g_web.voice_strings);
    u32  total = (u32)mel_speech_web__voices_refresh();
    u32  n = total < cap ? total : cap;
    char buf[512];
    for (u32 i = 0; i < n; i++)
    {
        mel_speech_web__voice_field((int)i, 0, buf, sizeof buf);
        str8 uri = web_intern(&g_web.voice_strings, buf);
        mel_speech_web__voice_field((int)i, 1, buf, sizeof buf);
        str8 name = web_intern(&g_web.voice_strings, buf);
        mel_speech_web__voice_field((int)i, 2, buf, sizeof buf);
        str8 lang = web_intern(&g_web.voice_strings, buf);
        out[i] = (Mel_Speech_Voice_Raw){
            .stable_id = str8_hash(uri),
            .name = name,
            .language = lang,
            .caps = {
                .rate = true,
                .rate_min = 0.1f,
                .rate_max = 10.0f,
                .pitch = true,
                .volume = true,
                .ranges = true,
                .can_pause = true,
            },
        };
    }
    return total;
}

static u32 web_enumerate_recognizers(void* user, const Mel_Alloc* alloc, Mel_Speech_Recognizer_Raw* out, u32 cap)
{
    MEL_UNUSED(user);
    web_ensure(alloc);
    if (!mel_speech_web__stt_supported())
        return 0;
    if (cap >= 1)
    {
        web_strings_clear(&g_web.rec_strings);
        char buf[64];
        mel_speech_web__stt_lang(buf, sizeof buf);
        out[0] = (Mel_Speech_Recognizer_Raw){
            .stable_id = MEL_SPEECH_WEB_RECOGNIZER_ID,
            .language = web_intern(&g_web.rec_strings, buf),
            .caps = {
                .on_device = false,
                .partials = true,
                .can_stop = true,
            },
        };
    }
    return 1;
}

static const str8* web_voice_uri_by_id(u64 stable_id)
{
    for (usize i = 0; i + 2 < g_web.voice_strings.count; i += 3)
        if (str8_hash(g_web.voice_strings.items[i]) == stable_id)
            return &g_web.voice_strings.items[i];
    return NULL;
}

static Mel_Speech_Status web_speak(void* user, u64 stable_id, u64 token, const Mel_Speech_Speak_Lowered* lowered, Mel_Speech_Sink sink)
{
    MEL_UNUSED(user);
    const str8* uri = web_voice_uri_by_id(stable_id);
    if (!uri)
    {
        mel_log_error("speech", "web speak: voice %llu not found", (unsigned long long)stable_id);
        return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_NO_DEVICE;
    }
    Web_Job job = { .token = token, .sink = sink };
    mel_array_push(&g_web.tts_jobs, job);
    int ok = mel_speech_web__speak((double)(u32)token,
                                   (double)(u32)(token >> 32),
                                   (const char*)lowered->text.data,
                                   (int)lowered->text.len,
                                   (const char*)uri->data,
                                   lowered->rate,
                                   lowered->pitch,
                                   lowered->volume,
                                   lowered->want_ranges ? 1 : 0);
    if (!ok)
    {
        web_job_remove(&g_web.tts_jobs, token);
        mel_log_error("speech", "web speak: speechSynthesis unavailable");
        return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_UNSUPPORTED;
    }
    return MEL_SPEECH_OK;
}

static void web_speak_pause(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    mel_speech_web__tts_pause();
}

static void web_speak_resume(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    mel_speech_web__tts_resume();
}

static void web_speak_abort(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    web_job_remove(&g_web.tts_jobs, token);
    mel_speech_web__tts_cancel();
}

static const mel_speech_auth* web_authorization(void* user)
{
    MEL_UNUSED(user);
    switch (mel_speech_web__auth_state())
    {
    case 1:
        return &mel_speech_auth_granted;
    case 2:
        return &mel_speech_auth_denied;
    default:
        return &mel_speech_auth_not_determined;
    }
}

static void web_authorize(void* user, Mel_Speech_Sink sink)
{
    MEL_UNUSED(user);
    if (sink.on_auth == NULL)
        return;
    g_web.auth_sink = sink;
    g_web.auth_pending = true;
    mel_speech_web__authorize();
}

static Mel_Speech_Status web_listen(void* user, u64 stable_id, u64 token, const Mel_Speech_Listen_Lowered* lowered, Mel_Speech_Sink sink)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    if (g_web.rec_strings.count == 0)
        return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_NO_DEVICE;
    Web_Job job = { .token = token, .sink = sink };
    mel_array_push(&g_web.stt_jobs, job);
    int ok = mel_speech_web__listen((double)(u32)token, (double)(u32)(token >> 32), (const char*)g_web.rec_strings.items[0].data, lowered->partials ? 1 : 0);
    if (!ok)
    {
        web_job_remove(&g_web.stt_jobs, token);
        mel_log_error("speech", "web listen: SpeechRecognition unavailable or failed to start");
        return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_UNSUPPORTED;
    }
    return MEL_SPEECH_OK;
}

static void web_listen_stop(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    mel_speech_web__listen_stop((double)(u32)token, (double)(u32)(token >> 32));
}

static void web_listen_abort(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    web_job_remove(&g_web.stt_jobs, token);
    mel_speech_web__listen_abort((double)(u32)token, (double)(u32)(token >> 32));
}

static void web_shutdown(void* user, const Mel_Alloc* alloc)
{
    MEL_UNUSED(user);
    MEL_UNUSED(alloc);
    if (g_web.alloc == NULL)
        return;
    mel_speech_web__tts_cancel();
    web_strings_clear(&g_web.voice_strings);
    web_strings_clear(&g_web.rec_strings);
    mel_array_free(&g_web.voice_strings);
    mel_array_free(&g_web.rec_strings);
    mel_array_free(&g_web.tts_jobs);
    mel_array_free(&g_web.stt_jobs);
    if (g_web.text_buf)
        mel_dealloc(g_web.alloc, g_web.text_buf);
    memset(&g_web, 0, sizeof g_web);
}

void mel_speech__register_host_providers(void)
{
    static const Mel_Speech_Provider_Desc desc = {
        .name = "web-speech",
        .enumerate_voices = web_enumerate_voices,
        .enumerate_recognizers = web_enumerate_recognizers,
        .speak = web_speak,
        .speak_pause = web_speak_pause,
        .speak_resume = web_speak_resume,
        .speak_abort = web_speak_abort,
        .authorization = web_authorization,
        .authorize = web_authorize,
        .listen = web_listen,
        .listen_stop = web_listen_stop,
        .listen_abort = web_listen_abort,
        .shutdown = web_shutdown,
    };
    mel_speech_provider_register(&desc);
}
