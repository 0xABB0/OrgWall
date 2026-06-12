#include <core/platform.h>

#if !MEL_PLATFORM_ANDROID
#error "android-only translation unit"
#endif

#include <tts/provider.h>

#include <allocator/allocator.h>
#include <collection/array.h>
#include <string/str8.h>
#include <log/log.h>
#include <thread/mutex.h>
#include <platform/android/jni.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define MEL_TTS_ANDROID_DONE_OK        0
#define MEL_TTS_ANDROID_DONE_ABORTED   1
#define MEL_TTS_ANDROID_DONE_ERROR     2

#define MEL_TTS_ANDROID_RATE_MIN       0.1f
#define MEL_TTS_ANDROID_RATE_MAX       4.0f

#define MEL_TTS_ANDROID_WAV_PCM        1
#define MEL_TTS_ANDROID_WAV_FLOAT      3
#define MEL_TTS_ANDROID_WAV_EXTENSIBLE 0xFFFE

typedef struct
{
    u64          token;
    Mel_Tts_Sink sink;
    f32          volume;
    bool         render;
} Droid_Job;

typedef Mel_Array(Droid_Job) Droid_Jobs;

typedef struct
{
    u64     stable_id;
    str8    name;
    str8    language;
    jobject native_ref;
} Droid_Voice;

typedef Mel_Array(Droid_Voice) Droid_Voices;

typedef struct
{
    const Mel_Alloc* alloc;
    jclass           cls;
    bool             natives_registered;
    bool             ranges_supported;
    bool             init_pending_logged;
    Droid_Jobs       jobs;
    Droid_Voices     voices;
} Droid;

static Droid     g_droid;
static Mel_Mutex g_jobs_mutex;
static bool      g_jobs_mutex_ready;

static bool droid_job_take(u64 token, Droid_Job* out)
{
    bool found = false;
    mel_mutex_lock(&g_jobs_mutex);
    if (g_droid.alloc)
    {
        for (usize i = 0; i < g_droid.jobs.count; i++)
        {
            if (g_droid.jobs.items[i].token == token)
            {
                *out = g_droid.jobs.items[i];
                g_droid.jobs.items[i] = g_droid.jobs.items[g_droid.jobs.count - 1];
                g_droid.jobs.count--;
                found = true;
                break;
            }
        }
    }
    mel_mutex_unlock(&g_jobs_mutex);
    return found;
}

static bool droid_job_peek(u64 token, Droid_Job* out)
{
    bool found = false;
    mel_mutex_lock(&g_jobs_mutex);
    if (g_droid.alloc)
    {
        for (usize i = 0; i < g_droid.jobs.count; i++)
        {
            if (g_droid.jobs.items[i].token == token)
            {
                *out = g_droid.jobs.items[i];
                found = true;
                break;
            }
        }
    }
    mel_mutex_unlock(&g_jobs_mutex);
    return found;
}

static void droid_job_push(Droid_Job job)
{
    mel_mutex_lock(&g_jobs_mutex);
    mel_array_push(&g_droid.jobs, job);
    mel_mutex_unlock(&g_jobs_mutex);
}

static Mel_Tts_Status droid_done_status(int code)
{
    if (code == MEL_TTS_ANDROID_DONE_OK)
        return MEL_TTS_OK;
    if (code == MEL_TTS_ANDROID_DONE_ABORTED)
        return MEL_TTS_OK | MEL_TTS_RESULT_ABORTED;
    return MEL_TTS_ERROR;
}

static void JNICALL droid_native_ready(JNIEnv* env, jclass cls, jboolean ok)
{
    MEL_UNUSED(env);
    MEL_UNUSED(cls);
    if (ok == JNI_TRUE)
        mel_log_info("tts", "android: TextToSpeech engine ready; call mel_tts_refresh() to enumerate voices");
    else
        mel_log_error("tts", "android: TextToSpeech engine init failed; no voices will enumerate");
}

static void JNICALL droid_native_done(JNIEnv* env, jclass cls, jlong token, jint code)
{
    MEL_UNUSED(env);
    MEL_UNUSED(cls);
    Droid_Job job;
    if (!droid_job_take((u64)token, &job))
        return;
    if (job.sink.on_done)
        job.sink.on_done(job.sink.token, droid_done_status((int)code));
}

static void JNICALL droid_native_range(JNIEnv* env, jclass cls, jlong token, jint offset, jint length)
{
    MEL_UNUSED(env);
    MEL_UNUSED(cls);
    Droid_Job job;
    if (!droid_job_peek((u64)token, &job))
        return;
    if (job.sink.on_range)
        job.sink.on_range(job.sink.token, (Mel_Tts_Range){ .offset = (usize)offset, .length = (usize)length });
}

static u32 droid_wav_u32(const u8* p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }

static u16 droid_wav_u16(const u8* p) { return (u16)((u16)p[0] | ((u16)p[1] << 8)); }

static bool droid_wav_parse(const u8* data, usize len, const Mel_Alloc* alloc, Mel_Tts_Render* out, f32** out_frames)
{
    if (len < 12 || memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0)
    {
        mel_log_error("tts", "android render: output is not a RIFF/WAVE file");
        return false;
    }
    u16       format = 0;
    u16       channels = 0;
    u16       bits = 0;
    u32       sample_rate = 0;
    const u8* pcm = NULL;
    usize     pcm_len = 0;
    usize     off = 12;
    while (off + 8 <= len)
    {
        usize csz = droid_wav_u32(data + off + 4);
        if (off + 8 + csz > len)
            csz = len - off - 8;
        const u8* chunk = data + off + 8;
        if (memcmp(data + off, "fmt ", 4) == 0 && csz >= 16)
        {
            format = droid_wav_u16(chunk);
            channels = droid_wav_u16(chunk + 2);
            sample_rate = droid_wav_u32(chunk + 4);
            bits = droid_wav_u16(chunk + 14);
            if (format == MEL_TTS_ANDROID_WAV_EXTENSIBLE && csz >= 40)
                format = droid_wav_u16(chunk + 24);
        }
        else if (memcmp(data + off, "data", 4) == 0)
        {
            pcm = chunk;
            pcm_len = csz;
        }
        off += 8 + csz + (csz & 1);
    }
    bool pcm_int = format == MEL_TTS_ANDROID_WAV_PCM && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
    bool pcm_f32 = format == MEL_TTS_ANDROID_WAV_FLOAT && bits == 32;
    if (!pcm || pcm_len == 0 || channels == 0 || sample_rate == 0 || (!pcm_int && !pcm_f32))
    {
        mel_log_error("tts", "android render: unsupported WAV (format=%u bits=%u channels=%u rate=%u data=%zu)", (u32)format, (u32)bits, (u32)channels, sample_rate, pcm_len);
        return false;
    }
    usize sample_size = (usize)bits / 8;
    usize frame_count = pcm_len / (sample_size * channels);
    usize samples = frame_count * channels;
    if (samples == 0)
    {
        mel_log_error("tts", "android render: WAV data chunk holds zero frames");
        return false;
    }
    f32* frames = (f32*)mel_alloc(alloc, samples * sizeof(f32));
    if (!frames)
        return false;
    for (usize i = 0; i < samples; i++)
    {
        const u8* s = pcm + i * sample_size;
        f32       v;
        if (pcm_f32)
        {
            u32 raw = droid_wav_u32(s);
            memcpy(&v, &raw, sizeof v);
        }
        else if (bits == 16)
        {
            v = (f32)(i16)droid_wav_u16(s) / 32768.0f;
        }
        else if (bits == 8)
        {
            v = ((f32)s[0] - 128.0f) / 128.0f;
        }
        else if (bits == 24)
        {
            i32 raw = (i32)(((u32)s[0] << 8) | ((u32)s[1] << 16) | ((u32)s[2] << 24)) >> 8;
            v = (f32)raw / 8388608.0f;
        }
        else
        {
            v = (f32)(i32)droid_wav_u32(s) / 2147483648.0f;
        }
        frames[i] = v;
    }
    out->frames = frames;
    out->frame_count = (u32)frame_count;
    out->sample_rate = sample_rate;
    out->channels = (u32)channels;
    *out_frames = frames;
    return true;
}

static u8* droid_file_read(const char* path, const Mel_Alloc* alloc, usize* out_len)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz <= 0 || fseek(f, 0, SEEK_SET) != 0)
    {
        fclose(f);
        return NULL;
    }
    u8* buf = (u8*)mel_alloc(alloc, (usize)sz);
    if (!buf)
    {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (usize)sz, f) != (usize)sz)
    {
        mel_dealloc(alloc, buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_len = (usize)sz;
    return buf;
}

static void JNICALL droid_native_render_done(JNIEnv* env, jclass cls, jlong token, jstring jpath)
{
    MEL_UNUSED(cls);
    const char* path = jpath ? (*env)->GetStringUTFChars(env, jpath, NULL) : NULL;
    Droid_Job   job;
    if (!droid_job_take((u64)token, &job))
    {
        if (path)
        {
            remove(path);
            (*env)->ReleaseStringUTFChars(env, jpath, path);
        }
        return;
    }
    Mel_Tts_Render pcm = { 0 };
    f32*           frames = NULL;
    u8*            bytes = NULL;
    usize          len = 0;
    bool           ok = false;
    if (path)
    {
        bytes = droid_file_read(path, g_droid.alloc, &len);
        if (!bytes)
            mel_log_error("tts", "android render: cannot read synthesized file %s", path);
        else
            ok = droid_wav_parse(bytes, len, g_droid.alloc, &pcm, &frames);
    }
    else
    {
        mel_log_error("tts", "android render: helper delivered no output path");
    }
    if (ok && job.volume > 0.0f && job.volume != 1.0f)
    {
        usize samples = (usize)pcm.frame_count * pcm.channels;
        for (usize i = 0; i < samples; i++)
            frames[i] *= job.volume;
    }
    if (job.sink.on_render)
    {
        if (ok)
            job.sink.on_render(job.sink.token, &pcm, MEL_TTS_OK);
        else
            job.sink.on_render(job.sink.token, NULL, MEL_TTS_ERROR | MEL_TTS_RESULT_AUDIO);
    }
    if (frames)
        mel_dealloc(g_droid.alloc, frames);
    if (bytes)
        mel_dealloc(g_droid.alloc, bytes);
    if (path)
    {
        remove(path);
        (*env)->ReleaseStringUTFChars(env, jpath, path);
    }
}

static jclass droid_class(JNIEnv* env)
{
    if (g_droid.cls != NULL)
        return g_droid.cls;
    jclass cls = mel_platform_android_find_class(env, "orgwall/melody/tts/MelodyTts");
    if (cls == NULL)
    {
        (*env)->ExceptionClear(env);
        mel_log_error("tts", "android: MelodyTts Java helper not found");
        return NULL;
    }
    if (!g_droid.natives_registered)
    {
        JNINativeMethod methods[] = {
            { "nativeReady", "(Z)V", (void*)droid_native_ready },
            { "nativeDone", "(JI)V", (void*)droid_native_done },
            { "nativeRange", "(JII)V", (void*)droid_native_range },
            { "nativeRenderDone", "(JLjava/lang/String;)V", (void*)droid_native_render_done },
        };
        if ((*env)->RegisterNatives(env, cls, methods, 4) != 0)
        {
            (*env)->ExceptionClear(env);
            mel_log_error("tts", "android: RegisterNatives failed for MelodyTts");
            return NULL;
        }
        g_droid.natives_registered = true;
    }
    g_droid.cls = (jclass)(*env)->NewGlobalRef(env, cls);
    return g_droid.cls;
}

static bool droid_call_bool(JNIEnv* env, jclass cls, const char* name, const char* sig, ...)
{
    jmethodID m = (*env)->GetStaticMethodID(env, cls, name, sig);
    if (!m)
    {
        (*env)->ExceptionClear(env);
        mel_log_error("tts", "android: MelodyTts.%s missing", name);
        return false;
    }
    va_list args;
    va_start(args, sig);
    jboolean r = (*env)->CallStaticBooleanMethodV(env, cls, m, args);
    va_end(args);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return false;
    }
    return r == JNI_TRUE;
}

static i32 droid_call_int(JNIEnv* env, jclass cls, const char* name, const char* sig, ...)
{
    jmethodID m = (*env)->GetStaticMethodID(env, cls, name, sig);
    if (!m)
    {
        (*env)->ExceptionClear(env);
        mel_log_error("tts", "android: MelodyTts.%s missing", name);
        return 0;
    }
    va_list args;
    va_start(args, sig);
    jint r = (*env)->CallStaticIntMethodV(env, cls, m, args);
    va_end(args);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return 0;
    }
    return (i32)r;
}

static void droid_call_void(JNIEnv* env, jclass cls, const char* name, const char* sig, ...)
{
    jmethodID m = (*env)->GetStaticMethodID(env, cls, name, sig);
    if (!m)
    {
        (*env)->ExceptionClear(env);
        mel_log_error("tts", "android: MelodyTts.%s missing", name);
        return;
    }
    va_list args;
    va_start(args, sig);
    (*env)->CallStaticVoidMethodV(env, cls, m, args);
    va_end(args);
    if ((*env)->ExceptionCheck(env))
        (*env)->ExceptionClear(env);
}

static jobject droid_call_object(JNIEnv* env, jclass cls, const char* name, const char* sig, ...)
{
    jmethodID m = (*env)->GetStaticMethodID(env, cls, name, sig);
    if (!m)
    {
        (*env)->ExceptionClear(env);
        mel_log_error("tts", "android: MelodyTts.%s missing", name);
        return NULL;
    }
    va_list args;
    va_start(args, sig);
    jobject r = (*env)->CallStaticObjectMethodV(env, cls, m, args);
    va_end(args);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    return r;
}

static str8 droid_intern_cstr(const char* utf8)
{
    usize len = utf8 ? strlen(utf8) : 0;
    u8*   data = (u8*)mel_alloc(g_droid.alloc, len + 1);
    if (!data)
        return STR8_EMPTY;
    if (len)
        memcpy(data, utf8, len);
    data[len] = 0;
    return (str8){ data, (size)len };
}

static str8 droid_intern_jstring(JNIEnv* env, jstring js)
{
    if (js == NULL)
        return STR8_EMPTY;
    const char* utf8 = (*env)->GetStringUTFChars(env, js, NULL);
    str8        s = droid_intern_cstr(utf8 ? utf8 : "");
    if (utf8)
        (*env)->ReleaseStringUTFChars(env, js, utf8);
    (*env)->DeleteLocalRef(env, js);
    return s;
}

static void droid_voices_clear(JNIEnv* env)
{
    for (usize i = 0; i < g_droid.voices.count; i++)
    {
        Droid_Voice* v = &g_droid.voices.items[i];
        if (v->name.data)
            mel_dealloc(g_droid.alloc, v->name.data);
        if (v->language.data)
            mel_dealloc(g_droid.alloc, v->language.data);
        if (v->native_ref && env)
            (*env)->DeleteGlobalRef(env, v->native_ref);
    }
    mel_array_clear(&g_droid.voices);
}

static Droid_Voice* droid_voice_by_id(u64 stable_id)
{
    for (usize i = 0; i < g_droid.voices.count; i++)
        if (g_droid.voices.items[i].stable_id == stable_id)
            return &g_droid.voices.items[i];
    return NULL;
}

static void droid_ensure(const Mel_Alloc* alloc)
{
    if (g_droid.alloc != NULL)
        return;
    if (!g_jobs_mutex_ready)
    {
        mel_mutex_init(&g_jobs_mutex, MEL_MUTEX_PLAIN);
        g_jobs_mutex_ready = true;
    }
    g_droid.alloc = alloc;
    mel_array_init(&g_droid.jobs, alloc);
    mel_array_init(&g_droid.voices, alloc);
}

static u32 droid_enumerate_voices(void* user, const Mel_Alloc* alloc, Mel_Tts_Voice_Raw* out, u32 cap)
{
    MEL_UNUSED(user);
    droid_ensure(alloc);
    JNIEnv* env = mel_platform_android_env();
    if (!env)
    {
        mel_log_error("tts", "android: no JNI environment; cannot enumerate voices");
        return 0;
    }
    if ((*env)->PushLocalFrame(env, 32) != 0)
        return 0;
    jclass cls = droid_class(env);
    if (!cls)
    {
        (*env)->PopLocalFrame(env, NULL);
        return 0;
    }
    droid_call_void(env, cls, "ensure", "()V");
    if (!droid_call_bool(env, cls, "ready", "()Z"))
    {
        if (!g_droid.init_pending_logged)
        {
            g_droid.init_pending_logged = true;
            mel_log_info("tts", "android: TextToSpeech engine still initializing; refresh after the ready log line");
        }
        (*env)->PopLocalFrame(env, NULL);
        return 0;
    }
    g_droid.ranges_supported = droid_call_bool(env, cls, "rangesSupported", "()Z");
    u32 total = (u32)droid_call_int(env, cls, "voicesRefresh", "()I");
    u32 n = total < cap ? total : cap;
    droid_voices_clear(env);
    for (u32 i = 0; i < n; i++)
    {
        str8        name = droid_intern_jstring(env, (jstring)droid_call_object(env, cls, "voiceName", "(I)Ljava/lang/String;", (jint)i));
        str8        lang = droid_intern_jstring(env, (jstring)droid_call_object(env, cls, "voiceLang", "(I)Ljava/lang/String;", (jint)i));
        Droid_Voice voice = { .stable_id = str8_hash(name), .name = name, .language = lang, .native_ref = NULL };
        mel_array_push(&g_droid.voices, voice);
        out[i] = (Mel_Tts_Voice_Raw){
            .stable_id = voice.stable_id,
            .name = name,
            .language = lang,
            .caps = {
                .rate = true,
                .rate_min = MEL_TTS_ANDROID_RATE_MIN,
                .rate_max = MEL_TTS_ANDROID_RATE_MAX,
                .pitch = true,
                .volume = true,
                .ranges = g_droid.ranges_supported,
                .can_pause = false,
                .render = true,
                .ssml = false,
                .visemes = false,
            },
        };
    }
    (*env)->PopLocalFrame(env, NULL);
    return total;
}

static jbyteArray droid_text_bytes(JNIEnv* env, str8 text)
{
    jbyteArray arr = (*env)->NewByteArray(env, (jsize)text.len);
    if (!arr)
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    (*env)->SetByteArrayRegion(env, arr, 0, (jsize)text.len, (const jbyte*)text.data);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    return arr;
}

static Mel_Tts_Status droid_submit(u64 stable_id, u64 token, const Mel_Tts_Lowered* lowered, Mel_Tts_Sink sink, bool render)
{
    if (lowered->ssml)
    {
        mel_log_error("tts", "android: ssml reached the provider despite caps.ssml == false; core bug");
        return MEL_TTS_ERROR | MEL_TTS_RESULT_UNSUPPORTED;
    }
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return MEL_TTS_ERROR;
    Droid_Voice* voice = droid_voice_by_id(stable_id);
    if (!voice)
    {
        mel_log_error("tts", "android: voice %llu not found; re-enumerate", (unsigned long long)stable_id);
        return MEL_TTS_ERROR | MEL_TTS_RESULT_LOST;
    }
    if ((*env)->PushLocalFrame(env, 16) != 0)
        return MEL_TTS_ERROR;
    jclass cls = droid_class(env);
    if (!cls)
    {
        (*env)->PopLocalFrame(env, NULL);
        return MEL_TTS_ERROR;
    }
    jbyteArray jtext = droid_text_bytes(env, lowered->text);
    jstring    jvoice = (*env)->NewStringUTF(env, (const char*)voice->name.data);
    if (!jtext || !jvoice)
    {
        (*env)->PopLocalFrame(env, NULL);
        return MEL_TTS_ERROR;
    }
    f32 volume = lowered->volume;
    if (!render && volume > 1.0f)
    {
        mel_log_warn("tts", "android: volume %.2f exceeds KEY_PARAM_VOLUME ceiling 1.0; clamped", volume);
        volume = 1.0f;
    }
    Droid_Job job = { .token = token, .sink = sink, .volume = lowered->volume, .render = render };
    droid_job_push(job);
    bool ok;
    if (render)
        ok = droid_call_bool(env, cls, "render", "([BLjava/lang/String;FFJ)Z", jtext, jvoice, (jfloat)lowered->rate, (jfloat)lowered->pitch, (jlong)token);
    else
        ok = droid_call_bool(env, cls, "speak", "([BLjava/lang/String;FFFZJ)Z", jtext, jvoice, (jfloat)lowered->rate, (jfloat)lowered->pitch, (jfloat)volume, (jboolean)(lowered->want_ranges ? JNI_TRUE : JNI_FALSE), (jlong)token);
    (*env)->PopLocalFrame(env, NULL);
    if (!ok)
    {
        Droid_Job dropped;
        droid_job_take(token, &dropped);
        mel_log_error("tts", "android: engine not ready; %s refused", render ? "render" : "speak");
        return MEL_TTS_ERROR | MEL_TTS_RESULT_BUSY;
    }
    return MEL_TTS_OK;
}

static Mel_Tts_Status droid_speak(void* user, u64 stable_id, u64 token, const Mel_Tts_Lowered* lowered, Mel_Tts_Sink sink)
{
    MEL_UNUSED(user);
    return droid_submit(stable_id, token, lowered, sink, false);
}

static Mel_Tts_Status droid_render(void* user, u64 stable_id, u64 token, const Mel_Tts_Lowered* lowered, Mel_Tts_Sink sink)
{
    MEL_UNUSED(user);
    return droid_submit(stable_id, token, lowered, sink, true);
}

static void droid_abort(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    if (!g_droid.alloc)
        return;
    Droid_Jobs snap;
    mel_mutex_lock(&g_jobs_mutex);
    snap = g_droid.jobs;
    mel_array_init(&g_droid.jobs, g_droid.alloc);
    mel_mutex_unlock(&g_jobs_mutex);
    JNIEnv* env = mel_platform_android_env();
    if (env && (*env)->PushLocalFrame(env, 8) == 0)
    {
        jclass cls = droid_class(env);
        if (cls)
            droid_call_void(env, cls, "stop", "()V");
        (*env)->PopLocalFrame(env, NULL);
    }
    if (snap.count > 1)
        mel_log_info("tts", "android: stop() purges the engine queue; resolving %u live utterance(s) ABORTED", (u32)snap.count);
    for (usize i = 0; i < snap.count; i++)
    {
        Droid_Job* job = &snap.items[i];
        if (job->sink.on_done)
            job->sink.on_done(job->sink.token, MEL_TTS_OK | MEL_TTS_RESULT_ABORTED);
    }
    mel_array_free(&snap);
}

static void* droid_voice_native(void* user, u64 stable_id)
{
    MEL_UNUSED(user);
    Droid_Voice* voice = g_droid.alloc ? droid_voice_by_id(stable_id) : NULL;
    if (!voice)
    {
        mel_log_error("tts", "android: voice_native on unknown voice %llu", (unsigned long long)stable_id);
        return NULL;
    }
    if (voice->native_ref)
        return voice->native_ref;
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return NULL;
    if ((*env)->PushLocalFrame(env, 8) != 0)
        return NULL;
    jclass cls = droid_class(env);
    if (cls)
    {
        jstring jname = (*env)->NewStringUTF(env, (const char*)voice->name.data);
        jobject obj = jname ? droid_call_object(env, cls, "voiceFind", "(Ljava/lang/String;)Landroid/speech/tts/Voice;", jname) : NULL;
        if (obj)
            voice->native_ref = (*env)->NewGlobalRef(env, obj);
    }
    (*env)->PopLocalFrame(env, NULL);
    if (!voice->native_ref)
        mel_log_error("tts", "android: no native Voice object for %.*s", (int)voice->name.len, voice->name.data);
    return voice->native_ref;
}

static void droid_shutdown(void* user, const Mel_Alloc* alloc)
{
    MEL_UNUSED(user);
    MEL_UNUSED(alloc);
    if (g_droid.alloc == NULL)
        return;
    JNIEnv* env = mel_platform_android_env();
    if (env && g_droid.cls)
    {
        droid_call_void(env, g_droid.cls, "shutdown", "()V");
        droid_voices_clear(env);
        (*env)->DeleteGlobalRef(env, g_droid.cls);
    }
    else
    {
        droid_voices_clear(NULL);
    }
    mel_array_free(&g_droid.voices);
    mel_mutex_lock(&g_jobs_mutex);
    mel_array_free(&g_droid.jobs);
    g_droid.alloc = NULL;
    mel_mutex_unlock(&g_jobs_mutex);
    memset(&g_droid, 0, sizeof g_droid);
}

void mel_tts__register_host_providers(void)
{
    static const Mel_Tts_Provider_Desc desc = {
        .name = "android-tts",
        .enumerate_voices = droid_enumerate_voices,
        .speak = droid_speak,
        .abort = droid_abort,
        .render = droid_render,
        .voice_native = droid_voice_native,
        .shutdown = droid_shutdown,
    };
    mel_tts_provider_register(&desc);
}
