#include <tts/provider.h>

#include <allocator/allocator.h>
#include <collection/array.h>
#include <string/str8.h>
#include <log/log.h>
#include <thread/thread.h>
#include <thread/mutex.h>

#include <math.h>
#include <string.h>
#include <wchar.h>
#include <stdatomic.h>

#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <initguid.h>
#include <sapi.h>

DEFINE_GUID(SPDFID_WaveFormatEx, 0xC31ADBAE, 0x527F, 0x4FF5, 0xA2, 0x30, 0xF6, 0x2B, 0xB6, 0x1F, 0xF7, 0x0C);

#define MEL_TTS_SAPI_RENDER_HZ 22050u

typedef struct
{
    WCHAR* original;
    usize  original_len;
    WCHAR* spoken;
    usize  spoken_len;
    u32*   map;
    DWORD  flags;
} Sapi_Text;

typedef struct
{
    u64           token;
    ULONG         stream;
    Sapi_Text     text;
    bool          want_ranges;
    bool          want_visemes;
    Mel_Tts_Range last_range;
    Mel_Tts_Sink  sink;
} Sapi_Job;

typedef struct
{
    u64           token;
    ISpVoice*     voice;
    ISpStream*    stream;
    IStream*      mem;
    Sapi_Text     text;
    DWORD         flags;
    Mel_Tts_Sink  sink;
    Mel_Thread    thread;
    _Atomic(bool) aborted;
    _Atomic(bool) done;
} Sapi_Render;

typedef Mel_Array(str8) Sapi_Strings;

typedef struct
{
    u64             stable_id;
    ISpObjectToken* token;
} Sapi_Voice;

typedef struct
{
    const Mel_Alloc* alloc;
    bool             com_ready;
    bool             com_owned;

    ISpVoice*  voice;
    HANDLE     event;
    Mel_Thread thread;
    bool       run;
    Mel_Mutex  lock;
    Mel_Array(Sapi_Job) jobs;
    Mel_Array(Sapi_Render*) renders;
    Mel_Array(Sapi_Voice) voices;
    Sapi_Strings strings;
} Sapi;

static Sapi g_sapi;

static str8 sapi_utf8_from_wide(const WCHAR* wide)
{
    int len = wide ? WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL) : 0;
    if (len <= 0)
        return STR8_EMPTY;
    u8* data = (u8*)mel_alloc(g_sapi.alloc, (usize)len);
    if (!data)
        return STR8_EMPTY;
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, (char*)data, len, NULL, NULL);
    return (str8){ data, (size)(len - 1) };
}

static str8 sapi_intern_wide(Sapi_Strings* strings, const WCHAR* wide)
{
    str8 s = sapi_utf8_from_wide(wide);
    if (s.data)
        mel_array_push(strings, s);
    return s;
}

static void sapi_strings_clear(Sapi_Strings* strings)
{
    for (usize i = 0; i < strings->count; i++)
        if (strings->items[i].data)
            mel_dealloc(g_sapi.alloc, strings->items[i].data);
    mel_array_clear(strings);
}

static WCHAR* sapi_wide_from_str8(str8 s, usize* out_len)
{
    int wlen = MultiByteToWideChar(CP_UTF8, 0, (const char*)s.data, (int)s.len, NULL, 0);
    if (wlen <= 0)
        return NULL;
    WCHAR* wide = (WCHAR*)mel_alloc(g_sapi.alloc, ((usize)wlen + 1) * sizeof(WCHAR));
    if (!wide)
        return NULL;
    MultiByteToWideChar(CP_UTF8, 0, (const char*)s.data, (int)s.len, wide, wlen);
    wide[wlen] = 0;
    *out_len = (usize)wlen;
    return wide;
}

static usize sapi_utf8_offset(const WCHAR* wide, usize wchars)
{
    if (wchars == 0)
        return 0;
    int n = WideCharToMultiByte(CP_UTF8, 0, wide, (int)wchars, NULL, 0, NULL, NULL);
    return n > 0 ? (usize)n : 0;
}

static long sapi_rate_from(f32 multiplier)
{
    f32 r = 10.0f * (logf(multiplier) / logf(3.0f));
    if (r < -10.0f)
        r = -10.0f;
    if (r > 10.0f)
        r = 10.0f;
    return (long)lroundf(r);
}

static long sapi_pitch_from(f32 multiplier)
{
    f32 p = 10.0f * log2f(multiplier);
    if (p < -10.0f)
        p = -10.0f;
    if (p > 10.0f)
        p = 10.0f;
    return (long)lroundf(p);
}

static USHORT sapi_volume_from(f32 multiplier)
{
    if (multiplier <= 0.0f)
        return 100;
    f32 v = multiplier * 100.0f;
    if (v > 100.0f)
    {
        mel_log_warn("tts", "sapi: volume multiplier %.2f exceeds native ceiling; clamped to 1.0", (f64)multiplier);
        v = 100.0f;
    }
    return (USHORT)lroundf(v);
}

static const WCHAR* sapi_xml_entity(WCHAR c, usize* len)
{
    if (c == L'&')
    {
        *len = 5;
        return L"&amp;";
    }
    if (c == L'<')
    {
        *len = 4;
        return L"&lt;";
    }
    if (c == L'>')
    {
        *len = 4;
        return L"&gt;";
    }
    *len = 0;
    return NULL;
}

static void sapi_text_free(Sapi_Text* t)
{
    if (t->spoken && t->spoken != t->original)
        mel_dealloc(g_sapi.alloc, t->spoken);
    if (t->original)
        mel_dealloc(g_sapi.alloc, t->original);
    if (t->map)
        mel_dealloc(g_sapi.alloc, t->map);
    memset(t, 0, sizeof *t);
}

static bool sapi_text_wrap_pitch(Sapi_Text* t, long pitch)
{
    WCHAR prefix[32];
    int   plen = swprintf(prefix, countof(prefix), L"<pitch absmiddle=\"%ld\">", pitch);
    if (plen <= 0)
        return false;
    static const WCHAR suffix[] = L"</pitch>";
    usize              slen = lengthof(suffix);
    usize              body = 0;
    for (usize i = 0; i < t->original_len; i++)
    {
        usize elen;
        sapi_xml_entity(t->original[i], &elen);
        body += elen ? elen : 1;
    }
    usize  total = (usize)plen + body + slen;
    WCHAR* spoken = (WCHAR*)mel_alloc(g_sapi.alloc, (total + 1) * sizeof(WCHAR));
    u32*   map = (u32*)mel_alloc(g_sapi.alloc, total * sizeof(u32));
    if (!spoken || !map)
    {
        if (spoken)
            mel_dealloc(g_sapi.alloc, spoken);
        if (map)
            mel_dealloc(g_sapi.alloc, map);
        return false;
    }
    usize w = 0;
    for (int i = 0; i < plen; i++)
    {
        map[w] = 0;
        spoken[w++] = prefix[i];
    }
    for (usize i = 0; i < t->original_len; i++)
    {
        usize        elen;
        const WCHAR* ent = sapi_xml_entity(t->original[i], &elen);
        if (ent)
        {
            for (usize k = 0; k < elen; k++)
            {
                map[w] = (u32)i;
                spoken[w++] = ent[k];
            }
        }
        else
        {
            map[w] = (u32)i;
            spoken[w++] = t->original[i];
        }
    }
    for (usize i = 0; i < slen; i++)
    {
        map[w] = (u32)t->original_len;
        spoken[w++] = suffix[i];
    }
    spoken[w] = 0;
    t->spoken = spoken;
    t->spoken_len = w;
    t->map = map;
    return true;
}

static bool sapi_text_build(const Mel_Tts_Lowered* lowered, Sapi_Text* t)
{
    memset(t, 0, sizeof *t);
    t->original = sapi_wide_from_str8(lowered->text, &t->original_len);
    if (!t->original)
        return false;
    t->spoken = t->original;
    t->spoken_len = t->original_len;
    if (lowered->ssml)
    {
        if (lowered->pitch != 0.0f)
            mel_log_warn("tts", "sapi: pitch multiplier dropped on ssml input; use <prosody pitch> in the document");
        t->flags = SPF_IS_XML | SPF_PARSE_SSML;
        return true;
    }
    if (lowered->pitch != 0.0f)
    {
        if (!sapi_text_wrap_pitch(t, sapi_pitch_from(lowered->pitch)))
        {
            sapi_text_free(t);
            return false;
        }
        t->flags = SPF_IS_XML | SPF_PARSE_SAPI;
        return true;
    }
    t->flags = SPF_IS_NOT_XML;
    return true;
}

static usize sapi_orig_off(const Sapi_Job* job, usize spoken_off)
{
    if (job->text.map == NULL)
        return spoken_off > job->text.original_len ? job->text.original_len : spoken_off;
    if (spoken_off >= job->text.spoken_len)
        return job->text.original_len;
    return (usize)job->text.map[spoken_off];
}

static Sapi_Job* sapi_job_by_stream(ULONG stream)
{
    for (usize i = 0; i < g_sapi.jobs.count; i++)
        if (g_sapi.jobs.items[i].stream == stream)
            return &g_sapi.jobs.items[i];
    return NULL;
}

static void sapi_job_remove(Sapi_Job* job)
{
    sapi_text_free(&job->text);
    usize idx = (usize)(job - g_sapi.jobs.items);
    g_sapi.jobs.items[idx] = g_sapi.jobs.items[g_sapi.jobs.count - 1];
    g_sapi.jobs.count--;
}

static void sapi_drain(void)
{
    SPEVENT ev;
    ULONG   fetched = 0;
    while (g_sapi.voice && ISpVoice_GetEvents(g_sapi.voice, 1, &ev, &fetched) == S_OK && fetched == 1)
    {
        mel_mutex_lock(&g_sapi.lock);
        Sapi_Job* job = sapi_job_by_stream(ev.ulStreamNum);
        if (!job)
        {
            mel_mutex_unlock(&g_sapi.lock);
            continue;
        }
        if (ev.eEventId == SPEI_END_INPUT_STREAM)
        {
            Mel_Tts_Sink sink = job->sink;
            sapi_job_remove(job);
            mel_mutex_unlock(&g_sapi.lock);
            if (sink.on_done)
                sink.on_done(sink.token, MEL_TTS_OK);
        }
        else if (ev.eEventId == SPEI_WORD_BOUNDARY)
        {
            Mel_Tts_Sink  sink = job->sink;
            bool          want = job->want_ranges;
            usize         woff = (usize)ev.lParam;
            usize         wlen = (usize)ev.wParam;
            Mel_Tts_Range range = { 0 };
            bool          valid = false;
            if (wlen > 0 && (job->want_ranges || job->want_visemes))
            {
                usize o0 = sapi_orig_off(job, woff);
                usize o1 = sapi_orig_off(job, woff + wlen);
                usize b0 = sapi_utf8_offset(job->text.original, o0);
                usize b1 = sapi_utf8_offset(job->text.original, o1);
                if (b1 >= b0)
                {
                    range = (Mel_Tts_Range){ .offset = b0, .length = b1 - b0 };
                    job->last_range = range;
                    valid = true;
                }
            }
            mel_mutex_unlock(&g_sapi.lock);
            if (valid && want && sink.on_range)
                sink.on_range(sink.token, range);
        }
        else if (ev.eEventId == SPEI_VISEME)
        {
            Mel_Tts_Sink   sink = job->sink;
            bool           want = job->want_visemes;
            Mel_Tts_Viseme vis = { .viseme = (u32)ev.lParam, .range = job->last_range };
            mel_mutex_unlock(&g_sapi.lock);
            if (want && sink.on_viseme)
                sink.on_viseme(sink.token, vis);
        }
        else
            mel_mutex_unlock(&g_sapi.lock);
    }
}

static int sapi_notify_main(void* user)
{
    MEL_UNUSED(user);
    while (g_sapi.run)
    {
        WaitForSingleObject(g_sapi.event, INFINITE);
        if (!g_sapi.run)
            break;
        sapi_drain();
    }
    return 0;
}

static bool sapi_com_ensure(void)
{
    if (g_sapi.com_ready)
        return true;
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
    {
        mel_log_error("tts", "sapi: CoInitializeEx failed (0x%08lx)", (unsigned long)hr);
        return false;
    }
    g_sapi.com_owned = hr != RPC_E_CHANGED_MODE;
    g_sapi.com_ready = true;
    return true;
}

static bool sapi_voice_ensure(void)
{
    if (g_sapi.voice)
        return true;
    if (!sapi_com_ensure())
        return false;
    HRESULT hr = CoCreateInstance(&CLSID_SpVoice, NULL, CLSCTX_ALL, &IID_ISpVoice, (void**)&g_sapi.voice);
    if (FAILED(hr) || !g_sapi.voice)
    {
        mel_log_error("tts", "sapi: SpVoice create failed (0x%08lx)", (unsigned long)hr);
        g_sapi.voice = NULL;
        return false;
    }
    ULONGLONG interest = SPFEI(SPEI_END_INPUT_STREAM) | SPFEI(SPEI_WORD_BOUNDARY) | SPFEI(SPEI_VISEME);
    ISpVoice_SetInterest(g_sapi.voice, interest, interest);
    ISpVoice_SetNotifyWin32Event(g_sapi.voice);
    g_sapi.event = ISpVoice_GetNotifyEventHandle(g_sapi.voice);
    g_sapi.run = true;
    if (!mel_thread_spawn(&g_sapi.thread, sapi_notify_main, NULL, .name = "mel-sapi-tts"))
    {
        g_sapi.run = false;
        ISpVoice_Release(g_sapi.voice);
        g_sapi.voice = NULL;
        return false;
    }
    return true;
}

static void sapi_voices_release(void)
{
    for (usize i = 0; i < g_sapi.voices.count; i++)
        if (g_sapi.voices.items[i].token)
            ISpObjectToken_Release(g_sapi.voices.items[i].token);
    mel_array_clear(&g_sapi.voices);
}

static ISpObjectToken* sapi_voice_token_by_id(u64 stable_id)
{
    for (usize i = 0; i < g_sapi.voices.count; i++)
        if (g_sapi.voices.items[i].stable_id == stable_id)
            return g_sapi.voices.items[i].token;
    return NULL;
}

static str8 sapi_language_of(ISpObjectToken* tok)
{
    ISpDataKey* attrs = NULL;
    if (FAILED(ISpObjectToken_OpenKey(tok, L"Attributes", &attrs)) || !attrs)
        return STR8_EMPTY;
    WCHAR* wlang = NULL;
    str8   out = STR8_EMPTY;
    if (SUCCEEDED(ISpDataKey_GetStringValue(attrs, L"Language", &wlang)) && wlang)
    {
        WCHAR* semi = wcschr(wlang, L';');
        if (semi)
            *semi = 0;
        LCID  lcid = (LCID)wcstoul(wlang, NULL, 16);
        WCHAR name[LOCALE_NAME_MAX_LENGTH];
        if (lcid != 0 && LCIDToLocaleName(lcid, name, LOCALE_NAME_MAX_LENGTH, 0) > 0)
            out = sapi_intern_wide(&g_sapi.strings, name);
        CoTaskMemFree(wlang);
    }
    ISpDataKey_Release(attrs);
    return out;
}

static u32 sapi_enumerate_voices(void* user, const Mel_Alloc* alloc, Mel_Tts_Voice_Raw* out, u32 cap)
{
    MEL_UNUSED(user);
    if (g_sapi.alloc == NULL)
    {
        g_sapi.alloc = alloc;
        mel_mutex_init(&g_sapi.lock, MEL_MUTEX_PLAIN);
        mel_array_init(&g_sapi.jobs, alloc);
        mel_array_init(&g_sapi.renders, alloc);
        mel_array_init(&g_sapi.voices, alloc);
        mel_array_init(&g_sapi.strings, alloc);
    }
    if (!sapi_com_ensure())
        return 0;

    ISpObjectTokenCategory* cat = NULL;
    if (FAILED(CoCreateInstance(&CLSID_SpObjectTokenCategory, NULL, CLSCTX_ALL, &IID_ISpObjectTokenCategory, (void**)&cat)) || !cat)
        return 0;
    if (FAILED(ISpObjectTokenCategory_SetId(cat, SPCAT_VOICES, FALSE)))
    {
        ISpObjectTokenCategory_Release(cat);
        return 0;
    }
    IEnumSpObjectTokens* it = NULL;
    if (FAILED(ISpObjectTokenCategory_EnumTokens(cat, NULL, NULL, &it)) || !it)
    {
        ISpObjectTokenCategory_Release(cat);
        return 0;
    }
    ULONG total = 0;
    IEnumSpObjectTokens_GetCount(it, &total);

    mel_mutex_lock(&g_sapi.lock);
    sapi_voices_release();
    sapi_strings_clear(&g_sapi.strings);
    u32 filled = 0;
    for (u32 i = 0; i < (u32)total && filled < cap; i++)
    {
        ISpObjectToken* tok = NULL;
        if (FAILED(IEnumSpObjectTokens_Item(it, i, &tok)) || !tok)
            break;
        WCHAR* wid = NULL;
        if (FAILED(ISpObjectToken_GetId(tok, &wid)) || !wid)
        {
            mel_log_error("tts", "sapi: voice token without id; skipping");
            ISpObjectToken_Release(tok);
            continue;
        }
        str8 idu = sapi_utf8_from_wide(wid);
        u64  stable_id = str8_hash(idu);

        WCHAR* wname = NULL;
        str8   name = STR8_EMPTY;
        if (SUCCEEDED(ISpObjectToken_GetStringValue(tok, NULL, &wname)) && wname)
        {
            name = sapi_intern_wide(&g_sapi.strings, wname);
            CoTaskMemFree(wname);
        }
        if (name.data == NULL)
        {
            mel_log_warn("tts", "sapi: voice %llu has no description; using token id as name", (unsigned long long)stable_id);
            name = sapi_intern_wide(&g_sapi.strings, wid);
        }
        CoTaskMemFree(wid);
        if (idu.data)
            mel_dealloc(g_sapi.alloc, idu.data);

        str8       lang = sapi_language_of(tok);
        Sapi_Voice v = { .stable_id = stable_id, .token = tok };
        mel_array_push(&g_sapi.voices, v);
        out[filled++] = (Mel_Tts_Voice_Raw){
            .stable_id = stable_id,
            .name = name,
            .language = lang,
            .viseme_set = S8("sapi"),
            .caps = {
                .rate = true,
                .rate_min = 0.33f,
                .rate_max = 3.0f,
                .pitch = true,
                .volume = true,
                .ranges = true,
                .can_pause = true,
                .render = true,
                .ssml = true,
                .visemes = true,
            },
        };
    }
    mel_mutex_unlock(&g_sapi.lock);
    IEnumSpObjectTokens_Release(it);
    ISpObjectTokenCategory_Release(cat);
    return (u32)total > cap ? (u32)total : filled;
}

static Mel_Tts_Status sapi_speak(void* user, u64 stable_id, u64 token, const Mel_Tts_Lowered* lowered, Mel_Tts_Sink sink)
{
    MEL_UNUSED(user);
    if (!sapi_voice_ensure())
        return MEL_TTS_ERROR | MEL_TTS_RESULT_AUDIO;

    mel_mutex_lock(&g_sapi.lock);
    ISpObjectToken* vtok = sapi_voice_token_by_id(stable_id);
    if (!vtok)
    {
        mel_mutex_unlock(&g_sapi.lock);
        mel_log_error("tts", "sapi speak: voice %llu not found", (unsigned long long)stable_id);
        return MEL_TTS_ERROR | MEL_TTS_RESULT_LOST;
    }
    ISpVoice_SetVoice(g_sapi.voice, vtok);
    ISpVoice_SetRate(g_sapi.voice, lowered->rate > 0.0f ? sapi_rate_from(lowered->rate) : 0);
    ISpVoice_SetVolume(g_sapi.voice, sapi_volume_from(lowered->volume));

    Sapi_Job job;
    memset(&job, 0, sizeof job);
    if (!sapi_text_build(lowered, &job.text))
    {
        mel_mutex_unlock(&g_sapi.lock);
        mel_log_error("tts", "sapi speak: text conversion failed");
        return MEL_TTS_ERROR;
    }
    ULONG   stream = 0;
    HRESULT hr = ISpVoice_Speak(g_sapi.voice, job.text.spoken, SPF_ASYNC | job.text.flags, &stream);
    if (FAILED(hr))
    {
        sapi_text_free(&job.text);
        mel_mutex_unlock(&g_sapi.lock);
        mel_log_error("tts", "sapi speak failed (0x%08lx)", (unsigned long)hr);
        return MEL_TTS_ERROR | MEL_TTS_RESULT_AUDIO;
    }
    job.token = token;
    job.stream = stream;
    job.want_ranges = lowered->want_ranges;
    job.want_visemes = lowered->want_visemes;
    job.sink = sink;
    mel_array_push(&g_sapi.jobs, job);
    mel_mutex_unlock(&g_sapi.lock);
    return MEL_TTS_OK;
}

static void sapi_pause(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    if (g_sapi.voice)
        ISpVoice_Pause(g_sapi.voice);
}

static void sapi_resume(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    if (g_sapi.voice)
        ISpVoice_Resume(g_sapi.voice);
}

static void sapi_abort(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    if (g_sapi.alloc == NULL)
        return;
    mel_mutex_lock(&g_sapi.lock);
    for (usize i = 0; i < g_sapi.renders.count; i++)
    {
        Sapi_Render* r = g_sapi.renders.items[i];
        if (r->token == token && !atomic_load(&r->done))
        {
            atomic_store(&r->aborted, true);
            ISpVoice_Speak(r->voice, NULL, SPF_PURGEBEFORESPEAK | SPF_ASYNC, NULL);
            mel_mutex_unlock(&g_sapi.lock);
            return;
        }
    }
    Mel_Array(Sapi_Job) purged;
    mel_array_init(&purged, g_sapi.alloc);
    for (usize i = 0; i < g_sapi.jobs.count; i++)
        mel_array_push(&purged, g_sapi.jobs.items[i]);
    mel_array_clear(&g_sapi.jobs);
    mel_mutex_unlock(&g_sapi.lock);

    if (g_sapi.voice)
        ISpVoice_Speak(g_sapi.voice, NULL, SPF_PURGEBEFORESPEAK | SPF_ASYNC, NULL);

    for (usize i = 0; i < purged.count; i++)
    {
        Sapi_Job* job = &purged.items[i];
        if (job->token != token && job->sink.on_done)
            job->sink.on_done(job->sink.token, MEL_TTS_OK | MEL_TTS_RESULT_ABORTED);
        sapi_text_free(&job->text);
    }
    mel_array_free(&purged);
}

static void sapi_render_destroy(Sapi_Render* r)
{
    if (r->stream)
        ISpStream_Release(r->stream);
    if (r->mem)
        IStream_Release(r->mem);
    if (r->voice)
        ISpVoice_Release(r->voice);
    sapi_text_free(&r->text);
    mel_dealloc(g_sapi.alloc, r);
}

static void sapi_renders_reap(bool all)
{
    for (usize i = 0; i < g_sapi.renders.count;)
    {
        Sapi_Render* r = g_sapi.renders.items[i];
        if (all && !atomic_load(&r->done))
        {
            atomic_store(&r->aborted, true);
            ISpVoice_Speak(r->voice, NULL, SPF_PURGEBEFORESPEAK | SPF_ASYNC, NULL);
        }
        if (all || atomic_load(&r->done))
        {
            mel_thread_join(&r->thread, NULL);
            sapi_render_destroy(r);
            g_sapi.renders.items[i] = g_sapi.renders.items[g_sapi.renders.count - 1];
            g_sapi.renders.count--;
        }
        else
            i++;
    }
}

static void sapi_render_deliver(Sapi_Render* r)
{
    LARGE_INTEGER  zero = { 0 };
    ULARGE_INTEGER end = { 0 };
    HGLOBAL        hg = NULL;
    if (FAILED(IStream_Seek(r->mem, zero, STREAM_SEEK_END, &end)) || FAILED(GetHGlobalFromStream(r->mem, &hg)) || hg == NULL)
    {
        mel_log_error("tts", "sapi render: output stream unreadable");
        if (r->sink.on_render)
            r->sink.on_render(r->sink.token, NULL, MEL_TTS_ERROR | MEL_TTS_RESULT_AUDIO);
        return;
    }
    u32 frames = (u32)(end.QuadPart / sizeof(i16));
    if (frames == 0)
    {
        mel_log_error("tts", "sapi render: engine produced no audio");
        if (r->sink.on_render)
            r->sink.on_render(r->sink.token, NULL, MEL_TTS_ERROR | MEL_TTS_RESULT_AUDIO);
        return;
    }
    const i16* samples = (const i16*)GlobalLock(hg);
    if (!samples)
    {
        mel_log_error("tts", "sapi render: GlobalLock failed (err=%lu)", (unsigned long)GetLastError());
        if (r->sink.on_render)
            r->sink.on_render(r->sink.token, NULL, MEL_TTS_ERROR | MEL_TTS_RESULT_AUDIO);
        return;
    }
    f32* pcm = (f32*)mel_alloc(g_sapi.alloc, sizeof(f32) * (usize)frames);
    if (!pcm)
    {
        GlobalUnlock(hg);
        if (r->sink.on_render)
            r->sink.on_render(r->sink.token, NULL, MEL_TTS_ERROR | MEL_TTS_RESULT_AUDIO);
        return;
    }
    for (u32 i = 0; i < frames; i++)
        pcm[i] = (f32)samples[i] * (1.0f / 32768.0f);
    GlobalUnlock(hg);
    Mel_Tts_Render out = { .frames = pcm, .frame_count = frames, .sample_rate = MEL_TTS_SAPI_RENDER_HZ, .channels = 1 };
    if (r->sink.on_render)
        r->sink.on_render(r->sink.token, &out, MEL_TTS_OK);
    mel_dealloc(g_sapi.alloc, pcm);
}

static int sapi_render_worker(void* user)
{
    Sapi_Render* r = user;
    HRESULT      co = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool         co_owned = !FAILED(co) && co != RPC_E_CHANGED_MODE;
    if (!atomic_load(&r->aborted))
    {
        HRESULT hr = ISpVoice_Speak(r->voice, r->text.spoken, r->flags, NULL);
        ISpStream_Close(r->stream);
        if (!atomic_load(&r->aborted))
        {
            if (FAILED(hr))
            {
                mel_log_error("tts", "sapi render speak failed (0x%08lx)", (unsigned long)hr);
                if (r->sink.on_render)
                    r->sink.on_render(r->sink.token, NULL, MEL_TTS_ERROR | MEL_TTS_RESULT_AUDIO);
            }
            else
                sapi_render_deliver(r);
        }
    }
    if (co_owned)
        CoUninitialize();
    atomic_store(&r->done, true);
    return 0;
}

static Mel_Tts_Status sapi_render(void* user, u64 stable_id, u64 token, const Mel_Tts_Lowered* lowered, Mel_Tts_Sink sink)
{
    MEL_UNUSED(user);
    if (!sapi_com_ensure())
        return MEL_TTS_ERROR | MEL_TTS_RESULT_AUDIO;

    mel_mutex_lock(&g_sapi.lock);
    sapi_renders_reap(false);
    ISpObjectToken* vtok = sapi_voice_token_by_id(stable_id);
    mel_mutex_unlock(&g_sapi.lock);
    if (!vtok)
    {
        mel_log_error("tts", "sapi render: voice %llu not found", (unsigned long long)stable_id);
        return MEL_TTS_ERROR | MEL_TTS_RESULT_LOST;
    }

    Sapi_Render* r = mel_alloc_type(g_sapi.alloc, Sapi_Render);
    if (!r)
        return MEL_TTS_ERROR;
    memset(r, 0, sizeof *r);
    r->token = token;
    r->sink = sink;

    HRESULT hr = CoCreateInstance(&CLSID_SpVoice, NULL, CLSCTX_ALL, &IID_ISpVoice, (void**)&r->voice);
    if (FAILED(hr) || !r->voice)
    {
        mel_log_error("tts", "sapi render: SpVoice create failed (0x%08lx)", (unsigned long)hr);
        r->voice = NULL;
        sapi_render_destroy(r);
        return MEL_TTS_ERROR | MEL_TTS_RESULT_AUDIO;
    }
    ISpVoice_SetVoice(r->voice, vtok);
    ISpVoice_SetRate(r->voice, lowered->rate > 0.0f ? sapi_rate_from(lowered->rate) : 0);
    ISpVoice_SetVolume(r->voice, sapi_volume_from(lowered->volume));

    hr = CreateStreamOnHGlobal(NULL, TRUE, &r->mem);
    if (FAILED(hr) || !r->mem)
    {
        mel_log_error("tts", "sapi render: CreateStreamOnHGlobal failed (0x%08lx)", (unsigned long)hr);
        r->mem = NULL;
        sapi_render_destroy(r);
        return MEL_TTS_ERROR | MEL_TTS_RESULT_AUDIO;
    }
    hr = CoCreateInstance(&CLSID_SpStream, NULL, CLSCTX_ALL, &IID_ISpStream, (void**)&r->stream);
    if (FAILED(hr) || !r->stream)
    {
        mel_log_error("tts", "sapi render: SpStream create failed (0x%08lx)", (unsigned long)hr);
        r->stream = NULL;
        sapi_render_destroy(r);
        return MEL_TTS_ERROR | MEL_TTS_RESULT_AUDIO;
    }
    WAVEFORMATEX fmt;
    memset(&fmt, 0, sizeof fmt);
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 1;
    fmt.nSamplesPerSec = MEL_TTS_SAPI_RENDER_HZ;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 2;
    fmt.nAvgBytesPerSec = MEL_TTS_SAPI_RENDER_HZ * 2;
    hr = ISpStream_SetBaseStream(r->stream, r->mem, &SPDFID_WaveFormatEx, &fmt);
    if (FAILED(hr))
    {
        mel_log_error("tts", "sapi render: SetBaseStream failed (0x%08lx)", (unsigned long)hr);
        sapi_render_destroy(r);
        return MEL_TTS_ERROR | MEL_TTS_RESULT_AUDIO;
    }
    hr = ISpVoice_SetOutput(r->voice, (IUnknown*)r->stream, FALSE);
    if (FAILED(hr))
    {
        mel_log_error("tts", "sapi render: SetOutput failed (0x%08lx)", (unsigned long)hr);
        sapi_render_destroy(r);
        return MEL_TTS_ERROR | MEL_TTS_RESULT_AUDIO;
    }
    if (!sapi_text_build(lowered, &r->text))
    {
        mel_log_error("tts", "sapi render: text conversion failed");
        sapi_render_destroy(r);
        return MEL_TTS_ERROR;
    }
    r->flags = r->text.flags;

    mel_mutex_lock(&g_sapi.lock);
    mel_array_push(&g_sapi.renders, r);
    mel_mutex_unlock(&g_sapi.lock);
    if (!mel_thread_spawn(&r->thread, sapi_render_worker, r, .name = "mel-sapi-render"))
    {
        mel_log_error("tts", "sapi render: worker spawn failed");
        mel_mutex_lock(&g_sapi.lock);
        for (usize i = 0; i < g_sapi.renders.count; i++)
        {
            if (g_sapi.renders.items[i] == r)
            {
                g_sapi.renders.items[i] = g_sapi.renders.items[g_sapi.renders.count - 1];
                g_sapi.renders.count--;
                break;
            }
        }
        mel_mutex_unlock(&g_sapi.lock);
        sapi_render_destroy(r);
        return MEL_TTS_ERROR;
    }
    return MEL_TTS_OK;
}

static void* sapi_voice_native(void* user, u64 stable_id)
{
    MEL_UNUSED(user);
    if (g_sapi.alloc == NULL)
        return NULL;
    mel_mutex_lock(&g_sapi.lock);
    ISpObjectToken* tok = sapi_voice_token_by_id(stable_id);
    mel_mutex_unlock(&g_sapi.lock);
    return (void*)tok;
}

static void sapi_shutdown(void* user, const Mel_Alloc* alloc)
{
    MEL_UNUSED(user);
    MEL_UNUSED(alloc);
    if (g_sapi.alloc == NULL)
        return;
    if (g_sapi.voice)
    {
        g_sapi.run = false;
        ISpVoice_Speak(g_sapi.voice, NULL, SPF_PURGEBEFORESPEAK | SPF_ASYNC, NULL);
        SetEvent(g_sapi.event);
        mel_thread_join(&g_sapi.thread, NULL);
        ISpVoice_Release(g_sapi.voice);
        g_sapi.voice = NULL;
    }
    mel_mutex_lock(&g_sapi.lock);
    for (usize i = 0; i < g_sapi.jobs.count; i++)
    {
        mel_log_warn("tts", "sapi: utterance %llu still live at shutdown; dropping", (unsigned long long)g_sapi.jobs.items[i].token);
        sapi_text_free(&g_sapi.jobs.items[i].text);
    }
    mel_array_clear(&g_sapi.jobs);
    sapi_renders_reap(true);
    mel_mutex_unlock(&g_sapi.lock);
    sapi_voices_release();
    sapi_strings_clear(&g_sapi.strings);
    mel_array_free(&g_sapi.strings);
    mel_array_free(&g_sapi.voices);
    mel_array_free(&g_sapi.renders);
    mel_array_free(&g_sapi.jobs);
    mel_mutex_destroy(&g_sapi.lock);
    if (g_sapi.com_owned)
        CoUninitialize();
    memset(&g_sapi, 0, sizeof g_sapi);
}

void mel_tts__register_host_providers(void)
{
    static const Mel_Tts_Provider_Desc desc = {
        .name = "win32-sapi",
        .enumerate_voices = sapi_enumerate_voices,
        .speak = sapi_speak,
        .pause = sapi_pause,
        .resume = sapi_resume,
        .abort = sapi_abort,
        .render = sapi_render,
        .voice_native = sapi_voice_native,
        .shutdown = sapi_shutdown,
    };
    mel_tts_provider_register(&desc);
}
