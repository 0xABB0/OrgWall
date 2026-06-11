#include <speech/provider.h>

#include <allocator/allocator.h>
#include <collection/array.h>
#include <string/str8.h>
#include <log/log.h>
#include <thread/thread.h>
#include <thread/mutex.h>

#include <math.h>
#include <string.h>

#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <initguid.h>
#include <sapi.h>

#define MEL_SPEECH_SAPI_RECOGNIZER_ID 0x736170695263ull

typedef struct
{
    u64             token;
    ULONG           stream;
    WCHAR*          wide;
    Mel_Speech_Sink sink;
} Sapi_Job;

typedef Mel_Array(Sapi_Job) Sapi_Jobs;
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

    ISpVoice*  voice;
    HANDLE     tts_event;
    Mel_Thread tts_thread;
    bool       tts_run;
    Mel_Mutex  lock;
    Sapi_Jobs  tts_jobs;
    Mel_Array(Sapi_Voice) voices;
    Sapi_Strings voice_strings;
    Sapi_Strings rec_strings;

    ISpRecognizer*  reco;
    ISpRecoContext* reco_ctx;
    ISpRecoGrammar* grammar;
    HANDLE          stt_event;
    Mel_Thread      stt_thread;
    bool            stt_run;
    bool            stt_active;
    u64             stt_token;
    Mel_Speech_Sink stt_sink;
} Sapi;

static Sapi g_sapi;

static str8 sapi_intern_wide(Sapi_Strings* strings, const WCHAR* wide)
{
    int len = wide ? WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL) : 0;
    if (len <= 0)
        return (str8){ 0 };
    u8* data = (u8*)mel_alloc(g_sapi.alloc, (usize)len);
    if (!data)
        return (str8){ 0 };
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, (char*)data, len, NULL, NULL);
    str8 s = { data, (size)(len - 1) };
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

static WCHAR* sapi_wide_from_str8(str8 s)
{
    int    wlen = MultiByteToWideChar(CP_UTF8, 0, (const char*)s.data, (int)s.len, NULL, 0);
    WCHAR* wide = (WCHAR*)mel_alloc(g_sapi.alloc, ((usize)wlen + 1) * sizeof(WCHAR));
    if (!wide)
        return NULL;
    MultiByteToWideChar(CP_UTF8, 0, (const char*)s.data, (int)s.len, wide, wlen);
    wide[wlen] = 0;
    return wide;
}

static usize sapi_utf8_offset(const WCHAR* wide, usize wchars)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, wide, (int)wchars, NULL, 0, NULL, NULL);
    return n > 0 ? (usize)n : 0;
}

static Sapi_Job* sapi_job_by_stream(ULONG stream)
{
    for (usize i = 0; i < g_sapi.tts_jobs.count; i++)
        if (g_sapi.tts_jobs.items[i].stream == stream)
            return &g_sapi.tts_jobs.items[i];
    return NULL;
}

static void sapi_job_remove(Sapi_Job* job)
{
    if (job->wide)
        mel_dealloc(g_sapi.alloc, job->wide);
    usize idx = (usize)(job - g_sapi.tts_jobs.items);
    g_sapi.tts_jobs.items[idx] = g_sapi.tts_jobs.items[g_sapi.tts_jobs.count - 1];
    g_sapi.tts_jobs.count--;
}

static void sapi_tts_drain(void)
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
            Mel_Speech_Sink sink = job->sink;
            sapi_job_remove(job);
            mel_mutex_unlock(&g_sapi.lock);
            if (sink.on_speak_done)
                sink.on_speak_done(sink.token, MEL_SPEECH_OK);
        }
        else if (ev.eEventId == SPEI_WORD_BOUNDARY)
        {
            Mel_Speech_Sink sink = job->sink;
            usize           woff = (usize)ev.lParam;
            usize           wlen = (usize)ev.wParam;
            usize           off8 = sapi_utf8_offset(job->wide, woff);
            usize           end8 = sapi_utf8_offset(job->wide, woff + wlen);
            mel_mutex_unlock(&g_sapi.lock);
            if (sink.on_range && end8 >= off8)
                sink.on_range(sink.token, (Mel_Speech_Range){ .offset = off8, .length = end8 - off8 });
        }
        else
            mel_mutex_unlock(&g_sapi.lock);
    }
}

static int sapi_tts_notify_main(void* user)
{
    MEL_UNUSED(user);
    while (g_sapi.tts_run)
    {
        WaitForSingleObject(g_sapi.tts_event, INFINITE);
        if (!g_sapi.tts_run)
            break;
        sapi_tts_drain();
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
        mel_log_error("speech", "sapi: CoInitializeEx failed (0x%08lx)", (unsigned long)hr);
        return false;
    }
    g_sapi.com_ready = true;
    return true;
}

static bool sapi_tts_ensure(void)
{
    if (g_sapi.voice)
        return true;
    if (!sapi_com_ensure())
        return false;
    HRESULT hr = CoCreateInstance(&CLSID_SpVoice, NULL, CLSCTX_ALL, &IID_ISpVoice, (void**)&g_sapi.voice);
    if (FAILED(hr) || !g_sapi.voice)
    {
        mel_log_error("speech", "sapi: SpVoice create failed (0x%08lx)", (unsigned long)hr);
        g_sapi.voice = NULL;
        return false;
    }
    ISpVoice_SetInterest(g_sapi.voice, SPFEI(SPEI_END_INPUT_STREAM) | SPFEI(SPEI_WORD_BOUNDARY), SPFEI(SPEI_END_INPUT_STREAM) | SPFEI(SPEI_WORD_BOUNDARY));
    ISpVoice_SetNotifyWin32Event(g_sapi.voice);
    g_sapi.tts_event = ISpVoice_GetNotifyEventHandle(g_sapi.voice);
    g_sapi.tts_run = true;
    if (!mel_thread_spawn(&g_sapi.tts_thread, sapi_tts_notify_main, NULL, .name = "mel-sapi-tts"))
    {
        g_sapi.tts_run = false;
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

static str8 sapi_language_of(ISpObjectToken* tok)
{
    ISpDataKey* attrs = NULL;
    if (FAILED(ISpObjectToken_OpenKey(tok, L"Attributes", &attrs)) || !attrs)
        return (str8){ 0 };
    WCHAR* wlang = NULL;
    str8   out = { 0 };
    if (SUCCEEDED(ISpDataKey_GetStringValue(attrs, L"Language", &wlang)) && wlang)
    {
        WCHAR* semi = wcschr(wlang, L';');
        if (semi)
            *semi = 0;
        LCID  lcid = (LCID)wcstoul(wlang, NULL, 16);
        WCHAR name[LOCALE_NAME_MAX_LENGTH];
        if (lcid != 0 && LCIDToLocaleName(lcid, name, LOCALE_NAME_MAX_LENGTH, 0) > 0)
            out = sapi_intern_wide(&g_sapi.voice_strings, name);
        CoTaskMemFree(wlang);
    }
    ISpDataKey_Release(attrs);
    return out;
}

static u32 sapi_enumerate_voices(void* user, const Mel_Alloc* alloc, Mel_Speech_Voice_Raw* out, u32 cap)
{
    MEL_UNUSED(user);
    if (g_sapi.alloc == NULL)
    {
        g_sapi.alloc = alloc;
        mel_mutex_init(&g_sapi.lock, MEL_MUTEX_PLAIN);
        mel_array_init(&g_sapi.tts_jobs, alloc);
        mel_array_init(&g_sapi.voices, alloc);
        mel_array_init(&g_sapi.voice_strings, alloc);
        mel_array_init(&g_sapi.rec_strings, alloc);
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
    sapi_strings_clear(&g_sapi.voice_strings);
    u32 n = (u32)total < cap ? (u32)total : cap;
    for (u32 i = 0; i < n; i++)
    {
        ISpObjectToken* tok = NULL;
        if (FAILED(IEnumSpObjectTokens_Item(it, i, &tok)) || !tok)
            break;
        WCHAR* wname = NULL;
        str8   name = { 0 };
        if (SUCCEEDED(ISpObjectToken_GetStringValue(tok, NULL, &wname)) && wname)
        {
            name = sapi_intern_wide(&g_sapi.voice_strings, wname);
            CoTaskMemFree(wname);
        }
        str8       lang = sapi_language_of(tok);
        Sapi_Voice v = { .stable_id = str8_hash(name), .token = tok };
        mel_array_push(&g_sapi.voices, v);
        out[i] = (Mel_Speech_Voice_Raw){
            .stable_id = v.stable_id,
            .name = name,
            .language = lang,
            .caps = {
                .rate = true,
                .rate_min = 0.33f,
                .rate_max = 3.0f,
                .pitch = false,
                .volume = true,
                .ranges = true,
                .can_pause = true,
            },
        };
    }
    mel_mutex_unlock(&g_sapi.lock);
    IEnumSpObjectTokens_Release(it);
    ISpObjectTokenCategory_Release(cat);
    return (u32)total;
}

static ISpObjectToken* sapi_voice_token_by_id(u64 stable_id)
{
    for (usize i = 0; i < g_sapi.voices.count; i++)
        if (g_sapi.voices.items[i].stable_id == stable_id)
            return g_sapi.voices.items[i].token;
    return NULL;
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

static Mel_Speech_Status sapi_speak(void* user, u64 stable_id, u64 token, const Mel_Speech_Speak_Lowered* lowered, Mel_Speech_Sink sink)
{
    MEL_UNUSED(user);
    if (!sapi_tts_ensure())
        return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_UNSUPPORTED;

    mel_mutex_lock(&g_sapi.lock);
    ISpObjectToken* vtok = sapi_voice_token_by_id(stable_id);
    if (!vtok)
    {
        mel_mutex_unlock(&g_sapi.lock);
        mel_log_error("speech", "sapi speak: voice %llu not found", (unsigned long long)stable_id);
        return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_NO_DEVICE;
    }
    ISpVoice_SetVoice(g_sapi.voice, vtok);
    ISpVoice_SetRate(g_sapi.voice, lowered->rate > 0.0f ? sapi_rate_from(lowered->rate) : 0);
    ISpVoice_SetVolume(g_sapi.voice, lowered->volume > 0.0f ? (USHORT)(lowered->volume * 100.0f) : 100);

    WCHAR* wide = sapi_wide_from_str8(lowered->text);
    if (!wide)
    {
        mel_mutex_unlock(&g_sapi.lock);
        return MEL_SPEECH_ERROR;
    }
    ULONG   stream = 0;
    HRESULT hr = ISpVoice_Speak(g_sapi.voice, wide, SPF_ASYNC, &stream);
    if (FAILED(hr))
    {
        mel_dealloc(g_sapi.alloc, wide);
        mel_mutex_unlock(&g_sapi.lock);
        mel_log_error("speech", "sapi speak failed (0x%08lx)", (unsigned long)hr);
        return MEL_SPEECH_ERROR;
    }
    Sapi_Job job = { .token = token, .stream = stream, .wide = wide, .sink = sink };
    mel_array_push(&g_sapi.tts_jobs, job);
    mel_mutex_unlock(&g_sapi.lock);
    return MEL_SPEECH_OK;
}

static void sapi_speak_pause(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    if (g_sapi.voice)
        ISpVoice_Pause(g_sapi.voice);
}

static void sapi_speak_resume(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    if (g_sapi.voice)
        ISpVoice_Resume(g_sapi.voice);
}

static void sapi_speak_abort(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    if (!g_sapi.voice)
        return;
    mel_mutex_lock(&g_sapi.lock);
    Mel_Array(Sapi_Job) others;
    mel_array_init(&others, g_sapi.alloc);
    for (usize i = 0; i < g_sapi.tts_jobs.count; i++)
    {
        if (g_sapi.tts_jobs.items[i].token != token)
            mel_array_push(&others, g_sapi.tts_jobs.items[i]);
        else if (g_sapi.tts_jobs.items[i].wide)
            mel_dealloc(g_sapi.alloc, g_sapi.tts_jobs.items[i].wide);
    }
    for (usize i = 0; i < others.count; i++)
        if (others.items[i].wide)
            mel_dealloc(g_sapi.alloc, others.items[i].wide);
    mel_array_clear(&g_sapi.tts_jobs);
    mel_mutex_unlock(&g_sapi.lock);

    ISpVoice_Speak(g_sapi.voice, NULL, SPF_PURGEBEFORESPEAK | SPF_ASYNC, NULL);

    for (usize i = 0; i < others.count; i++)
        if (others.items[i].sink.on_speak_done)
            others.items[i].sink.on_speak_done(others.items[i].sink.token, MEL_SPEECH_OK | MEL_SPEECH_RESULT_ABORTED);
    mel_array_free(&others);
}

static const mel_speech_auth* sapi_authorization(void* user)
{
    MEL_UNUSED(user);
    return &mel_speech_auth_granted;
}

static void sapi_stt_finish(Mel_Speech_Status status)
{
    if (!g_sapi.stt_active)
        return;
    g_sapi.stt_active = false;
    Mel_Speech_Sink sink = g_sapi.stt_sink;
    if (g_sapi.grammar)
        ISpRecoGrammar_SetDictationState(g_sapi.grammar, SPRS_INACTIVE);
    if (sink.on_listen_done)
        sink.on_listen_done(sink.token, status);
}

static f32 sapi_confidence(ISpRecoResult* result)
{
    SPPHRASE* phrase = NULL;
    f32       conf = 0.0f;
    if (SUCCEEDED(ISpRecoResult_GetPhrase(result, &phrase)) && phrase)
    {
        if (phrase->Rule.Confidence > 0)
            conf = 0.9f;
        else if (phrase->Rule.Confidence == 0)
            conf = 0.5f;
        else
            conf = 0.25f;
        CoTaskMemFree(phrase);
    }
    return conf;
}

static void sapi_stt_emit(ISpRecoResult* result, bool final)
{
    WCHAR* wtext = NULL;
    if (FAILED(ISpRecoResult_GetText(result, SP_GETWHOLEPHRASE, SP_GETWHOLEPHRASE, TRUE, &wtext, NULL)) || !wtext)
        return;
    int len = WideCharToMultiByte(CP_UTF8, 0, wtext, -1, NULL, 0, NULL, NULL);
    if (len > 0)
    {
        char* utf8 = (char*)mel_alloc(g_sapi.alloc, (usize)len);
        if (utf8)
        {
            WideCharToMultiByte(CP_UTF8, 0, wtext, -1, utf8, len, NULL, NULL);
            Mel_Speech_Result res = {
                .text = (str8){ (u8*)utf8, (size)(len - 1) },
                .final = final,
                .confidence = final ? sapi_confidence(result) : 0.0f,
            };
            if (g_sapi.stt_sink.on_result)
                g_sapi.stt_sink.on_result(g_sapi.stt_sink.token, &res);
            mel_dealloc(g_sapi.alloc, utf8);
        }
    }
    CoTaskMemFree(wtext);
}

static void sapi_stt_drain(void)
{
    SPEVENT ev;
    ULONG   fetched = 0;
    while (g_sapi.reco_ctx && ISpRecoContext_GetEvents(g_sapi.reco_ctx, 1, &ev, &fetched) == S_OK && fetched == 1)
    {
        if (!g_sapi.stt_active)
        {
            if (ev.elParamType == SPET_LPARAM_IS_OBJECT && ev.lParam)
                IUnknown_Release((IUnknown*)ev.lParam);
            continue;
        }
        if (ev.eEventId == SPEI_HYPOTHESIS && ev.elParamType == SPET_LPARAM_IS_OBJECT && ev.lParam)
        {
            sapi_stt_emit((ISpRecoResult*)ev.lParam, false);
            IUnknown_Release((IUnknown*)ev.lParam);
        }
        else if (ev.eEventId == SPEI_RECOGNITION && ev.elParamType == SPET_LPARAM_IS_OBJECT && ev.lParam)
        {
            sapi_stt_emit((ISpRecoResult*)ev.lParam, true);
            IUnknown_Release((IUnknown*)ev.lParam);
        }
        else if (ev.elParamType == SPET_LPARAM_IS_OBJECT && ev.lParam)
            IUnknown_Release((IUnknown*)ev.lParam);
    }
}

static int sapi_stt_notify_main(void* user)
{
    MEL_UNUSED(user);
    while (g_sapi.stt_run)
    {
        WaitForSingleObject(g_sapi.stt_event, INFINITE);
        if (!g_sapi.stt_run)
            break;
        sapi_stt_drain();
    }
    return 0;
}

static bool sapi_stt_ensure(void)
{
    if (g_sapi.reco_ctx)
        return true;
    if (!sapi_com_ensure())
        return false;
    HRESULT hr = CoCreateInstance(&CLSID_SpSharedRecognizer, NULL, CLSCTX_ALL, &IID_ISpRecognizer, (void**)&g_sapi.reco);
    if (FAILED(hr) || !g_sapi.reco)
    {
        mel_log_error("speech", "sapi: shared recognizer create failed (0x%08lx)", (unsigned long)hr);
        g_sapi.reco = NULL;
        return false;
    }
    hr = ISpRecognizer_CreateRecoContext(g_sapi.reco, &g_sapi.reco_ctx);
    if (FAILED(hr) || !g_sapi.reco_ctx)
    {
        ISpRecognizer_Release(g_sapi.reco);
        g_sapi.reco = NULL;
        g_sapi.reco_ctx = NULL;
        return false;
    }
    ISpRecoContext_SetInterest(g_sapi.reco_ctx, SPFEI(SPEI_RECOGNITION) | SPFEI(SPEI_HYPOTHESIS), SPFEI(SPEI_RECOGNITION) | SPFEI(SPEI_HYPOTHESIS));
    ISpRecoContext_SetNotifyWin32Event(g_sapi.reco_ctx);
    g_sapi.stt_event = ISpRecoContext_GetNotifyEventHandle(g_sapi.reco_ctx);
    hr = ISpRecoContext_CreateGrammar(g_sapi.reco_ctx, 0, &g_sapi.grammar);
    if (FAILED(hr) || !g_sapi.grammar)
    {
        ISpRecoContext_Release(g_sapi.reco_ctx);
        ISpRecognizer_Release(g_sapi.reco);
        g_sapi.reco = NULL;
        g_sapi.reco_ctx = NULL;
        g_sapi.grammar = NULL;
        return false;
    }
    ISpRecoGrammar_LoadDictation(g_sapi.grammar, NULL, SPLO_STATIC);
    g_sapi.stt_run = true;
    if (!mel_thread_spawn(&g_sapi.stt_thread, sapi_stt_notify_main, NULL, .name = "mel-sapi-stt"))
    {
        g_sapi.stt_run = false;
        return false;
    }
    return true;
}

static u32 sapi_enumerate_recognizers(void* user, const Mel_Alloc* alloc, Mel_Speech_Recognizer_Raw* out, u32 cap)
{
    MEL_UNUSED(user);
    MEL_UNUSED(alloc);
    if (g_sapi.alloc == NULL)
        return 0;
    if (cap >= 1)
    {
        sapi_strings_clear(&g_sapi.rec_strings);
        WCHAR name[LOCALE_NAME_MAX_LENGTH];
        str8  lang = { 0 };
        if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) > 0)
            lang = sapi_intern_wide(&g_sapi.rec_strings, name);
        out[0] = (Mel_Speech_Recognizer_Raw){
            .stable_id = MEL_SPEECH_SAPI_RECOGNIZER_ID,
            .language = lang,
            .caps = {
                .on_device = true,
                .partials = true,
                .can_stop = true,
            },
        };
    }
    return 1;
}

static Mel_Speech_Status sapi_listen(void* user, u64 stable_id, u64 token, const Mel_Speech_Listen_Lowered* lowered, Mel_Speech_Sink sink)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(lowered);
    if (!sapi_stt_ensure())
        return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_AUDIO;
    g_sapi.stt_token = token;
    g_sapi.stt_sink = sink;
    g_sapi.stt_active = true;
    HRESULT hr = ISpRecoGrammar_SetDictationState(g_sapi.grammar, SPRS_ACTIVE);
    if (FAILED(hr))
    {
        g_sapi.stt_active = false;
        mel_log_error("speech", "sapi listen: dictation activate failed (0x%08lx)", (unsigned long)hr);
        return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_AUDIO;
    }
    return MEL_SPEECH_OK;
}

static void sapi_listen_stop(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    sapi_stt_finish(MEL_SPEECH_OK);
}

static void sapi_listen_abort(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    g_sapi.stt_active = false;
    if (g_sapi.grammar)
        ISpRecoGrammar_SetDictationState(g_sapi.grammar, SPRS_INACTIVE);
}

static void sapi_shutdown(void* user, const Mel_Alloc* alloc)
{
    MEL_UNUSED(user);
    MEL_UNUSED(alloc);
    if (g_sapi.alloc == NULL)
        return;
    if (g_sapi.voice)
    {
        g_sapi.tts_run = false;
        ISpVoice_Speak(g_sapi.voice, NULL, SPF_PURGEBEFORESPEAK | SPF_ASYNC, NULL);
        SetEvent(g_sapi.tts_event);
        mel_thread_join(&g_sapi.tts_thread, NULL);
        ISpVoice_Release(g_sapi.voice);
        g_sapi.voice = NULL;
    }
    if (g_sapi.reco_ctx)
    {
        g_sapi.stt_run = false;
        g_sapi.stt_active = false;
        ISpRecoGrammar_SetDictationState(g_sapi.grammar, SPRS_INACTIVE);
        SetEvent(g_sapi.stt_event);
        mel_thread_join(&g_sapi.stt_thread, NULL);
        ISpRecoGrammar_Release(g_sapi.grammar);
        ISpRecoContext_Release(g_sapi.reco_ctx);
        ISpRecognizer_Release(g_sapi.reco);
        g_sapi.grammar = NULL;
        g_sapi.reco_ctx = NULL;
        g_sapi.reco = NULL;
    }
    mel_mutex_lock(&g_sapi.lock);
    for (usize i = 0; i < g_sapi.tts_jobs.count; i++)
        if (g_sapi.tts_jobs.items[i].wide)
            mel_dealloc(g_sapi.alloc, g_sapi.tts_jobs.items[i].wide);
    mel_mutex_unlock(&g_sapi.lock);
    sapi_voices_release();
    sapi_strings_clear(&g_sapi.voice_strings);
    sapi_strings_clear(&g_sapi.rec_strings);
    mel_array_free(&g_sapi.voice_strings);
    mel_array_free(&g_sapi.rec_strings);
    mel_array_free(&g_sapi.voices);
    mel_array_free(&g_sapi.tts_jobs);
    mel_mutex_destroy(&g_sapi.lock);
    memset(&g_sapi, 0, sizeof g_sapi);
}

void mel_speech__register_host_providers(void)
{
    static const Mel_Speech_Provider_Desc desc = {
        .name = "win32-sapi",
        .enumerate_voices = sapi_enumerate_voices,
        .enumerate_recognizers = sapi_enumerate_recognizers,
        .speak = sapi_speak,
        .speak_pause = sapi_speak_pause,
        .speak_resume = sapi_speak_resume,
        .speak_abort = sapi_speak_abort,
        .authorization = sapi_authorization,
        .listen = sapi_listen,
        .listen_stop = sapi_listen_stop,
        .listen_abort = sapi_listen_abort,
        .shutdown = sapi_shutdown,
    };
    mel_speech_provider_register(&desc);
}
