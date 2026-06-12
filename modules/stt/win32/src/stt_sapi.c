#include <stt/provider.h>

#include <allocator/allocator.h>
#include <collection/array.h>
#include <string/str8.h>
#include <log/log.h>
#include <thread/thread.h>
#include <thread/mutex.h>

#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <initguid.h>
#include <sapi.h>

typedef struct
{
    ISpStreamFormatVtbl* lpVtbl;
    _Atomic(LONG)        refs;
    const Mel_Alloc*     alloc;
    Mel_Mutex            lock;
    HANDLE               data_event;
    bool                 ended;
    u32                  sample_rate;
    usize                read_pos;
    u64                  position;
    Mel_Array(u8) bytes;
} Sapi_Stream;

typedef struct
{
    u64             stable_id;
    ISpObjectToken* token;
    ISpRecognizer*  reco;
} Sapi_Recognizer;

typedef struct
{
    u64             token;
    u64             stable_id;
    bool            partials;
    Mel_Stt_Sink    sink;
    ISpRecoContext* ctx;
    ISpRecoGrammar* grammar;
    Sapi_Stream*    stream;
    HANDLE          notify;
    Mel_Thread      thread;
    bool            thread_spawned;
    _Atomic(bool)   run;
} Sapi_Job;

typedef Mel_Array(str8) Sapi_Strings;
typedef Mel_Array(Sapi_Recognizer) Sapi_Recognizers;
typedef Mel_Array(Sapi_Job*) Sapi_Jobs;

typedef struct
{
    const Mel_Alloc* alloc;
    bool             com_ready;
    Mel_Mutex        lock;
    Sapi_Strings     strings;
    Sapi_Recognizers recognizers;
    Sapi_Jobs        jobs;
} Sapi_Stt;

static Sapi_Stt g_stt;

static bool sapi_com_ensure(void)
{
    if (g_stt.com_ready)
        return true;
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
    {
        mel_log_error("stt", "sapi: CoInitializeEx failed (0x%08lx)", (unsigned long)hr);
        return false;
    }
    g_stt.com_ready = true;
    return true;
}

static str8 sapi_utf8_from_wide(const WCHAR* w)
{
    if (w == NULL)
        return STR8_EMPTY;
    int bytes = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (bytes <= 0)
        return STR8_EMPTY;
    u8* buf = (u8*)mel_alloc(g_stt.alloc, (usize)bytes);
    if (buf == NULL)
        return STR8_EMPTY;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, (LPSTR)buf, bytes, NULL, NULL);
    return (str8){ buf, (size)(bytes - 1) };
}

static str8 sapi_intern_wide(const WCHAR* w)
{
    str8 s = sapi_utf8_from_wide(w);
    if (s.data)
        mel_array_push(&g_stt.strings, s);
    return s;
}

static void sapi_strings_clear(void)
{
    for (usize i = 0; i < g_stt.strings.count; i++)
        if (g_stt.strings.items[i].data)
            mel_dealloc(g_stt.alloc, g_stt.strings.items[i].data);
    mel_array_clear(&g_stt.strings);
}

static HRESULT STDMETHODCALLTYPE sapi_stream_query(ISpStreamFormat* self, REFIID riid, void** out)
{
    Sapi_Stream* s = (Sapi_Stream*)self;
    if (out == NULL)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ISequentialStream) || IsEqualGUID(riid, &IID_IStream) || IsEqualGUID(riid, &IID_ISpStreamFormat))
    {
        *out = self;
        atomic_fetch_add_explicit(&s->refs, 1, memory_order_relaxed);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE sapi_stream_addref(ISpStreamFormat* self)
{
    Sapi_Stream* s = (Sapi_Stream*)self;
    return (ULONG)(atomic_fetch_add_explicit(&s->refs, 1, memory_order_relaxed) + 1);
}

static ULONG STDMETHODCALLTYPE sapi_stream_release(ISpStreamFormat* self)
{
    Sapi_Stream* s = (Sapi_Stream*)self;
    LONG         refs = atomic_fetch_sub_explicit(&s->refs, 1, memory_order_acq_rel) - 1;
    if (refs == 0)
    {
        mel_array_free(&s->bytes);
        CloseHandle(s->data_event);
        mel_mutex_destroy(&s->lock);
        mel_dealloc(s->alloc, s);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE sapi_stream_read(ISpStreamFormat* self, void* pv, ULONG cb, ULONG* read_out)
{
    Sapi_Stream* s = (Sapi_Stream*)self;
    if (pv == NULL && cb > 0)
        return E_POINTER;
    u8*   dst = (u8*)pv;
    ULONG got = 0;
    while (got < cb)
    {
        mel_mutex_lock(&s->lock);
        usize avail = s->bytes.count - s->read_pos;
        if (avail == 0)
        {
            bool ended = s->ended;
            mel_mutex_unlock(&s->lock);
            if (ended)
                break;
            WaitForSingleObject(s->data_event, INFINITE);
            continue;
        }
        usize want = (usize)(cb - got);
        usize take = avail < want ? avail : want;
        memcpy(dst + got, s->bytes.items + s->read_pos, take);
        s->read_pos += take;
        if (s->read_pos == s->bytes.count)
        {
            mel_array_clear(&s->bytes);
            s->read_pos = 0;
        }
        mel_mutex_unlock(&s->lock);
        got += (ULONG)take;
    }
    s->position += got;
    if (read_out)
        *read_out = got;
    return got == cb ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE sapi_stream_write(ISpStreamFormat* self, const void* pv, ULONG cb, ULONG* written)
{
    MEL_UNUSED(self);
    MEL_UNUSED(pv);
    MEL_UNUSED(cb);
    MEL_UNUSED(written);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE sapi_stream_seek(ISpStreamFormat* self, LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* pos)
{
    Sapi_Stream* s = (Sapi_Stream*)self;
    if (origin == STREAM_SEEK_CUR && move.QuadPart == 0)
    {
        if (pos)
            pos->QuadPart = s->position;
        return S_OK;
    }
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE sapi_stream_set_size(ISpStreamFormat* self, ULARGE_INTEGER new_size)
{
    MEL_UNUSED(self);
    MEL_UNUSED(new_size);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE sapi_stream_copy_to(ISpStreamFormat* self, IStream* stm, ULARGE_INTEGER cb, ULARGE_INTEGER* read_out, ULARGE_INTEGER* written)
{
    MEL_UNUSED(self);
    MEL_UNUSED(stm);
    MEL_UNUSED(cb);
    MEL_UNUSED(read_out);
    MEL_UNUSED(written);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE sapi_stream_commit(ISpStreamFormat* self, DWORD flags)
{
    MEL_UNUSED(self);
    MEL_UNUSED(flags);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE sapi_stream_revert(ISpStreamFormat* self)
{
    MEL_UNUSED(self);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE sapi_stream_lock_region(ISpStreamFormat* self, ULARGE_INTEGER offset, ULARGE_INTEGER cb, DWORD lock_type)
{
    MEL_UNUSED(self);
    MEL_UNUSED(offset);
    MEL_UNUSED(cb);
    MEL_UNUSED(lock_type);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE sapi_stream_unlock_region(ISpStreamFormat* self, ULARGE_INTEGER offset, ULARGE_INTEGER cb, DWORD lock_type)
{
    MEL_UNUSED(self);
    MEL_UNUSED(offset);
    MEL_UNUSED(cb);
    MEL_UNUSED(lock_type);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE sapi_stream_stat(ISpStreamFormat* self, STATSTG* stat, DWORD flags)
{
    MEL_UNUSED(self);
    MEL_UNUSED(flags);
    if (stat == NULL)
        return E_POINTER;
    memset(stat, 0, sizeof *stat);
    stat->type = STGTY_STREAM;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sapi_stream_clone(ISpStreamFormat* self, IStream** out)
{
    MEL_UNUSED(self);
    MEL_UNUSED(out);
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE sapi_stream_get_format(ISpStreamFormat* self, GUID* fmt_id, WAVEFORMATEX** out)
{
    Sapi_Stream* s = (Sapi_Stream*)self;
    if (fmt_id == NULL || out == NULL)
        return E_POINTER;
    WAVEFORMATEX* fmt = (WAVEFORMATEX*)CoTaskMemAlloc(sizeof *fmt);
    if (fmt == NULL)
        return E_OUTOFMEMORY;
    fmt->wFormatTag = WAVE_FORMAT_PCM;
    fmt->nChannels = 1;
    fmt->nSamplesPerSec = (DWORD)s->sample_rate;
    fmt->wBitsPerSample = 16u;
    fmt->nBlockAlign = 2u;
    fmt->nAvgBytesPerSec = (DWORD)(s->sample_rate * 2u);
    fmt->cbSize = 0;
    *fmt_id = SPDFID_WaveFormatEx;
    *out = fmt;
    return S_OK;
}

static ISpStreamFormatVtbl g_stream_vtbl = {
    .QueryInterface = sapi_stream_query,
    .AddRef = sapi_stream_addref,
    .Release = sapi_stream_release,
    .Read = sapi_stream_read,
    .Write = sapi_stream_write,
    .Seek = sapi_stream_seek,
    .SetSize = sapi_stream_set_size,
    .CopyTo = sapi_stream_copy_to,
    .Commit = sapi_stream_commit,
    .Revert = sapi_stream_revert,
    .LockRegion = sapi_stream_lock_region,
    .UnlockRegion = sapi_stream_unlock_region,
    .Stat = sapi_stream_stat,
    .Clone = sapi_stream_clone,
    .GetFormat = sapi_stream_get_format,
};

static Sapi_Stream* sapi_stream_create(u32 sample_rate)
{
    Sapi_Stream* s = mel_alloc_type(g_stt.alloc, Sapi_Stream);
    if (s == NULL)
        return NULL;
    memset(s, 0, sizeof *s);
    s->lpVtbl = &g_stream_vtbl;
    s->alloc = g_stt.alloc;
    atomic_store_explicit(&s->refs, 1, memory_order_relaxed);
    s->sample_rate = sample_rate;
    s->data_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (s->data_event == NULL)
    {
        mel_log_error("stt", "sapi: CreateEventW(stream) failed (err=%lu)", (unsigned long)GetLastError());
        mel_dealloc(g_stt.alloc, s);
        return NULL;
    }
    mel_mutex_init(&s->lock, MEL_MUTEX_PLAIN);
    mel_array_init(&s->bytes, g_stt.alloc);
    return s;
}

static void sapi_stream_append(Sapi_Stream* s, const f32* frames, u32 frame_count)
{
    if (frame_count == 0)
        return;
    mel_mutex_lock(&s->lock);
    usize needed = s->bytes.count + (usize)frame_count * 2u;
    if (needed > s->bytes.capacity)
    {
        usize grown = s->bytes.capacity == 0 ? (usize)MEL_DA_INIT_CAP : s->bytes.capacity * 2;
        while (grown < needed)
            grown *= 2;
        mel_array_reserve(&s->bytes, grown);
    }
    i16* dst = (i16*)(s->bytes.items + s->bytes.count);
    for (u32 i = 0; i < frame_count; i++)
    {
        f32 v = frames[i];
        if (v > 1.0f)
            v = 1.0f;
        if (v < -1.0f)
            v = -1.0f;
        dst[i] = (i16)lroundf(v * 32767.0f);
    }
    s->bytes.count = needed;
    mel_mutex_unlock(&s->lock);
    SetEvent(s->data_event);
}

static void sapi_stream_end(Sapi_Stream* s)
{
    mel_mutex_lock(&s->lock);
    s->ended = true;
    mel_mutex_unlock(&s->lock);
    SetEvent(s->data_event);
}

static Sapi_Recognizer* sapi_recognizer_find(u64 stable_id)
{
    for (usize i = 0; i < g_stt.recognizers.count; i++)
        if (g_stt.recognizers.items[i].stable_id == stable_id)
            return &g_stt.recognizers.items[i];
    return NULL;
}

static Sapi_Job* sapi_job_find(u64 token)
{
    for (usize i = 0; i < g_stt.jobs.count; i++)
        if (g_stt.jobs.items[i]->token == token)
            return g_stt.jobs.items[i];
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
            out = sapi_intern_wide(name);
        CoTaskMemFree(wlang);
    }
    ISpDataKey_Release(attrs);
    return out;
}

static u32 sapi_enumerate_recognizers(void* user, const Mel_Alloc* alloc, Mel_Stt_Recognizer_Raw* out, u32 cap)
{
    MEL_UNUSED(user);
    if (g_stt.alloc == NULL)
    {
        g_stt.alloc = alloc;
        mel_mutex_init(&g_stt.lock, MEL_MUTEX_PLAIN);
        mel_array_init(&g_stt.strings, alloc);
        mel_array_init(&g_stt.recognizers, alloc);
        mel_array_init(&g_stt.jobs, alloc);
    }
    if (!sapi_com_ensure())
        return 0;

    ISpObjectTokenCategory* cat = NULL;
    if (FAILED(CoCreateInstance(&CLSID_SpObjectTokenCategory, NULL, CLSCTX_ALL, &IID_ISpObjectTokenCategory, (void**)&cat)) || !cat)
        return 0;
    if (FAILED(ISpObjectTokenCategory_SetId(cat, SPCAT_RECOGNIZERS, FALSE)))
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

    sapi_strings_clear();
    Sapi_Recognizers fresh;
    mel_array_init(&fresh, g_stt.alloc);
    u32 n = 0;
    for (ULONG i = 0; i < total; i++)
    {
        ISpObjectToken* tok = NULL;
        if (FAILED(IEnumSpObjectTokens_Item(it, i, &tok)) || !tok)
            continue;
        WCHAR* wid = NULL;
        if (FAILED(ISpObjectToken_GetId(tok, &wid)) || !wid)
        {
            ISpObjectToken_Release(tok);
            continue;
        }
        str8 id = sapi_utf8_from_wide(wid);
        CoTaskMemFree(wid);
        if (id.data == NULL)
        {
            ISpObjectToken_Release(tok);
            continue;
        }
        u64 sid = str8_hash(id);
        mel_dealloc(g_stt.alloc, id.data);

        Sapi_Recognizer  entry = { .stable_id = sid, .token = tok };
        Sapi_Recognizer* old = sapi_recognizer_find(sid);
        if (old)
        {
            entry.reco = old->reco;
            old->reco = NULL;
            if (old->token)
            {
                ISpObjectToken_Release(old->token);
                old->token = NULL;
            }
        }
        str8 lang = sapi_language_of(tok);
        mel_array_push(&fresh, entry);
        if (n < cap)
        {
            out[n] = (Mel_Stt_Recognizer_Raw){
                .stable_id = sid,
                .language = lang,
                .caps = {
                    .on_device = true,
                    .require_on_device = true,
                    .partials = true,
                    .can_stop = false,
                    .feed = true,
                    .device_select = false,
                    .vocabulary = false,
                    .punctuation = false,
                    .profanity_filter = false,
                },
            };
            n++;
        }
    }
    for (usize i = 0; i < g_stt.recognizers.count; i++)
    {
        if (g_stt.recognizers.items[i].reco)
            ISpRecognizer_Release(g_stt.recognizers.items[i].reco);
        if (g_stt.recognizers.items[i].token)
            ISpObjectToken_Release(g_stt.recognizers.items[i].token);
    }
    mel_array_free(&g_stt.recognizers);
    g_stt.recognizers = fresh;
    IEnumSpObjectTokens_Release(it);
    ISpObjectTokenCategory_Release(cat);
    return (u32)g_stt.recognizers.count;
}

static const mel_stt_auth* sapi_authorization(void* user)
{
    MEL_UNUSED(user);
    return &mel_stt_auth_granted;
}

static void sapi_authorize(void* user, Mel_Stt_Sink sink)
{
    MEL_UNUSED(user);
    if (sink.on_auth)
        sink.on_auth(sink.token, &mel_stt_auth_granted);
}

static ISpRecognizer* sapi_reco_ensure(Sapi_Recognizer* entry)
{
    if (entry->reco)
        return entry->reco;
    if (!sapi_com_ensure())
        return NULL;
    HRESULT hr = CoCreateInstance(&CLSID_SpInprocRecognizer, NULL, CLSCTX_ALL, &IID_ISpRecognizer, (void**)&entry->reco);
    if (FAILED(hr) || !entry->reco)
    {
        mel_log_error("stt", "sapi: in-proc recognizer create failed (0x%08lx)", (unsigned long)hr);
        entry->reco = NULL;
        return NULL;
    }
    hr = ISpRecognizer_SetRecognizer(entry->reco, entry->token);
    if (FAILED(hr))
    {
        mel_log_error("stt", "sapi: SetRecognizer failed for %llu (0x%08lx)", (unsigned long long)entry->stable_id, (unsigned long)hr);
        ISpRecognizer_Release(entry->reco);
        entry->reco = NULL;
        return NULL;
    }
    return entry->reco;
}

static ISpObjectToken* sapi_default_audioin_token(void)
{
    ISpObjectTokenCategory* cat = NULL;
    if (FAILED(CoCreateInstance(&CLSID_SpObjectTokenCategory, NULL, CLSCTX_ALL, &IID_ISpObjectTokenCategory, (void**)&cat)) || !cat)
        return NULL;
    ISpObjectToken* tok = NULL;
    WCHAR*          wid = NULL;
    if (SUCCEEDED(ISpObjectTokenCategory_SetId(cat, SPCAT_AUDIOIN, FALSE)) && SUCCEEDED(ISpObjectTokenCategory_GetDefaultTokenId(cat, &wid)) && wid)
    {
        if (FAILED(CoCreateInstance(&CLSID_SpObjectToken, NULL, CLSCTX_ALL, &IID_ISpObjectToken, (void**)&tok)) || !tok)
            tok = NULL;
        else if (FAILED(ISpObjectToken_SetId(tok, NULL, wid, FALSE)))
        {
            ISpObjectToken_Release(tok);
            tok = NULL;
        }
        CoTaskMemFree(wid);
    }
    ISpObjectTokenCategory_Release(cat);
    return tok;
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

static void sapi_job_emit(Sapi_Job* job, ISpRecoResult* result, bool final)
{
    WCHAR* wtext = NULL;
    if (FAILED(ISpRecoResult_GetText(result, SP_GETWHOLEPHRASE, SP_GETWHOLEPHRASE, TRUE, &wtext, NULL)) || !wtext)
        return;
    int len = WideCharToMultiByte(CP_UTF8, 0, wtext, -1, NULL, 0, NULL, NULL);
    if (len > 0)
    {
        char* utf8 = (char*)mel_alloc(g_stt.alloc, (usize)len);
        if (utf8)
        {
            WideCharToMultiByte(CP_UTF8, 0, wtext, -1, utf8, len, NULL, NULL);
            Mel_Stt_Result res = {
                .text = (str8){ (u8*)utf8, (size)(len - 1) },
                .final = final,
                .confidence = final ? sapi_confidence(result) : 0.0f,
            };
            if (job->sink.on_result)
                job->sink.on_result(job->sink.token, &res);
            mel_dealloc(g_stt.alloc, utf8);
        }
    }
    CoTaskMemFree(wtext);
}

static void sapi_job_drain(Sapi_Job* job)
{
    SPEVENT ev;
    ULONG   fetched = 0;
    while (job->ctx && ISpRecoContext_GetEvents(job->ctx, 1, &ev, &fetched) == S_OK && fetched == 1)
    {
        bool live = atomic_load_explicit(&job->run, memory_order_acquire);
        if (live && ev.eEventId == SPEI_HYPOTHESIS && job->partials && ev.elParamType == SPET_LPARAM_IS_OBJECT && ev.lParam)
            sapi_job_emit(job, (ISpRecoResult*)ev.lParam, false);
        else if (live && ev.eEventId == SPEI_RECOGNITION && ev.elParamType == SPET_LPARAM_IS_OBJECT && ev.lParam)
            sapi_job_emit(job, (ISpRecoResult*)ev.lParam, true);
        if (ev.elParamType == SPET_LPARAM_IS_OBJECT && ev.lParam)
            IUnknown_Release((IUnknown*)ev.lParam);
    }
}

static int sapi_job_thread(void* user)
{
    Sapi_Job* job = user;
    while (atomic_load_explicit(&job->run, memory_order_acquire))
    {
        WaitForSingleObject(job->notify, INFINITE);
        if (!atomic_load_explicit(&job->run, memory_order_acquire))
            break;
        sapi_job_drain(job);
    }
    return 0;
}

static void sapi_job_destroy(Sapi_Job* job)
{
    atomic_store_explicit(&job->run, false, memory_order_release);
    if (job->stream)
        sapi_stream_end(job->stream);
    if (job->grammar)
        ISpRecoGrammar_SetDictationState(job->grammar, SPRS_INACTIVE);
    if (job->thread_spawned)
    {
        SetEvent(job->notify);
        mel_thread_join(&job->thread, NULL);
    }
    if (job->grammar)
        ISpRecoGrammar_Release(job->grammar);
    if (job->ctx)
        ISpRecoContext_Release(job->ctx);
    Sapi_Recognizer* entry = sapi_recognizer_find(job->stable_id);
    if (entry && entry->reco)
        ISpRecognizer_SetInput(entry->reco, NULL, TRUE);
    if (job->stream)
        ISpStreamFormat_Release((ISpStreamFormat*)job->stream);
    mel_dealloc(g_stt.alloc, job);
}

static Mel_Stt_Status sapi_listen(void* user, u64 stable_id, u64 token, const Mel_Stt_Listen_Lowered* lowered, Mel_Stt_Sink sink)
{
    MEL_UNUSED(user);
    Sapi_Recognizer* entry = sapi_recognizer_find(stable_id);
    if (!entry)
    {
        mel_log_error("stt", "sapi listen: recognizer %llu not found", (unsigned long long)stable_id);
        return MEL_STT_ERROR | MEL_STT_RESULT_NO_DEVICE;
    }
    if (lowered->device_stable_id.len > 0)
    {
        mel_log_error("stt", "sapi listen: device door lowered onto a recognizer whose caps deny it; provider bug");
        return MEL_STT_ERROR | MEL_STT_RESULT_UNSUPPORTED;
    }
    ISpRecognizer* reco = sapi_reco_ensure(entry);
    if (!reco)
        return MEL_STT_ERROR | MEL_STT_RESULT_AUDIO;

    Sapi_Job* job = mel_alloc_type(g_stt.alloc, Sapi_Job);
    if (!job)
        return MEL_STT_ERROR;
    memset(job, 0, sizeof *job);
    job->token = token;
    job->stable_id = stable_id;
    job->partials = lowered->partials;
    job->sink = sink;

    HRESULT hr;
    if (lowered->feed)
    {
        job->stream = sapi_stream_create(lowered->feed_sample_rate);
        if (!job->stream)
        {
            sapi_job_destroy(job);
            return MEL_STT_ERROR;
        }
        hr = ISpRecognizer_SetInput(reco, (IUnknown*)job->stream, TRUE);
    }
    else
    {
        ISpObjectToken* in = sapi_default_audioin_token();
        if (!in)
        {
            mel_log_error("stt", "sapi listen: no default audio input token");
            sapi_job_destroy(job);
            return MEL_STT_ERROR | MEL_STT_RESULT_NO_DEVICE;
        }
        hr = ISpRecognizer_SetInput(reco, (IUnknown*)in, TRUE);
        ISpObjectToken_Release(in);
    }
    if (FAILED(hr))
    {
        mel_log_error("stt", "sapi listen: SetInput failed (0x%08lx)", (unsigned long)hr);
        sapi_job_destroy(job);
        return MEL_STT_ERROR | MEL_STT_RESULT_AUDIO;
    }

    hr = ISpRecognizer_CreateRecoContext(reco, &job->ctx);
    if (FAILED(hr) || !job->ctx)
    {
        mel_log_error("stt", "sapi listen: CreateRecoContext failed (0x%08lx)", (unsigned long)hr);
        job->ctx = NULL;
        sapi_job_destroy(job);
        return MEL_STT_ERROR | MEL_STT_RESULT_AUDIO;
    }
    ISpRecoContext_SetInterest(job->ctx, SPFEI(SPEI_RECOGNITION) | SPFEI(SPEI_HYPOTHESIS), SPFEI(SPEI_RECOGNITION) | SPFEI(SPEI_HYPOTHESIS));
    ISpRecoContext_SetNotifyWin32Event(job->ctx);
    job->notify = ISpRecoContext_GetNotifyEventHandle(job->ctx);
    hr = ISpRecoContext_CreateGrammar(job->ctx, 0, &job->grammar);
    if (FAILED(hr) || !job->grammar)
    {
        mel_log_error("stt", "sapi listen: CreateGrammar failed (0x%08lx)", (unsigned long)hr);
        job->grammar = NULL;
        sapi_job_destroy(job);
        return MEL_STT_ERROR | MEL_STT_RESULT_AUDIO;
    }
    hr = ISpRecoGrammar_LoadDictation(job->grammar, NULL, SPLO_STATIC);
    if (FAILED(hr))
    {
        mel_log_error("stt", "sapi listen: LoadDictation failed (0x%08lx)", (unsigned long)hr);
        sapi_job_destroy(job);
        return MEL_STT_ERROR | MEL_STT_RESULT_AUDIO;
    }

    atomic_store_explicit(&job->run, true, memory_order_release);
    if (!mel_thread_spawn(&job->thread, sapi_job_thread, job, .name = "mel-sapi-stt"))
    {
        mel_log_error("stt", "sapi listen: event thread spawn failed");
        atomic_store_explicit(&job->run, false, memory_order_release);
        sapi_job_destroy(job);
        return MEL_STT_ERROR;
    }
    job->thread_spawned = true;

    hr = ISpRecoGrammar_SetDictationState(job->grammar, SPRS_ACTIVE);
    if (FAILED(hr))
    {
        mel_log_error("stt", "sapi listen: dictation activate failed (0x%08lx)", (unsigned long)hr);
        sapi_job_destroy(job);
        return MEL_STT_ERROR | MEL_STT_RESULT_AUDIO;
    }

    mel_mutex_lock(&g_stt.lock);
    mel_array_push(&g_stt.jobs, job);
    mel_mutex_unlock(&g_stt.lock);
    return MEL_STT_OK;
}

static void sapi_abort(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    Sapi_Job* job = NULL;
    mel_mutex_lock(&g_stt.lock);
    for (usize i = 0; i < g_stt.jobs.count; i++)
    {
        if (g_stt.jobs.items[i]->token == token)
        {
            job = g_stt.jobs.items[i];
            mel_array_remove_unordered(&g_stt.jobs, i);
            break;
        }
    }
    mel_mutex_unlock(&g_stt.lock);
    if (job)
        sapi_job_destroy(job);
}

static Mel_Stt_Status sapi_feed(void* user, u64 stable_id, u64 token, const f32* frames, u32 frame_count)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    mel_mutex_lock(&g_stt.lock);
    Sapi_Job* job = sapi_job_find(token);
    if (!job)
    {
        mel_mutex_unlock(&g_stt.lock);
        mel_log_error("stt", "sapi feed: no live job for token %llu", (unsigned long long)token);
        return MEL_STT_ERROR | MEL_STT_RESULT_LOST;
    }
    if (!job->stream)
    {
        mel_mutex_unlock(&g_stt.lock);
        mel_log_error("stt", "sapi feed: session was not opened through the fed door; provider bug");
        return MEL_STT_ERROR | MEL_STT_RESULT_UNSUPPORTED;
    }
    sapi_stream_append(job->stream, frames, frame_count);
    mel_mutex_unlock(&g_stt.lock);
    return MEL_STT_OK;
}

static void* sapi_recognizer_native(void* user, u64 stable_id)
{
    MEL_UNUSED(user);
    Sapi_Recognizer* entry = sapi_recognizer_find(stable_id);
    return entry ? (void*)sapi_reco_ensure(entry) : NULL;
}

static void sapi_shutdown(void* user, const Mel_Alloc* alloc)
{
    MEL_UNUSED(user);
    MEL_UNUSED(alloc);
    if (g_stt.alloc == NULL)
        return;
    Sapi_Jobs jobs;
    mel_mutex_lock(&g_stt.lock);
    jobs = g_stt.jobs;
    mel_array_init(&g_stt.jobs, g_stt.alloc);
    mel_mutex_unlock(&g_stt.lock);
    for (usize i = 0; i < jobs.count; i++)
        sapi_job_destroy(jobs.items[i]);
    mel_array_free(&jobs);

    for (usize i = 0; i < g_stt.recognizers.count; i++)
    {
        if (g_stt.recognizers.items[i].reco)
            ISpRecognizer_Release(g_stt.recognizers.items[i].reco);
        if (g_stt.recognizers.items[i].token)
            ISpObjectToken_Release(g_stt.recognizers.items[i].token);
    }
    mel_array_free(&g_stt.recognizers);
    sapi_strings_clear();
    mel_array_free(&g_stt.strings);
    mel_array_free(&g_stt.jobs);
    mel_mutex_destroy(&g_stt.lock);
    memset(&g_stt, 0, sizeof g_stt);
}

void mel_stt__register_host_providers(void)
{
    static const Mel_Stt_Provider_Desc desc = {
        .name = "win32-sapi",
        .enumerate_recognizers = sapi_enumerate_recognizers,
        .authorization = sapi_authorization,
        .authorize = sapi_authorize,
        .listen = sapi_listen,
        .stop = NULL,
        .abort = sapi_abort,
        .feed = sapi_feed,
        .recognizer_native = sapi_recognizer_native,
        .shutdown = sapi_shutdown,
    };
    mel_stt_provider_register(&desc);
}
