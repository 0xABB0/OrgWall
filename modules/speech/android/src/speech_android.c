#include <speech/provider.h>

#include <allocator/allocator.h>
#include <collection/array.h>
#include <string/str8.h>
#include <log/log.h>
#include <platform/android/jni.h>

#include <stdarg.h>
#include <string.h>

#define MEL_SPEECH_ANDROID_RECOGNIZER_ID 0x616E64537263ull
#define MEL_SPEECH_ANDROID_PERM_CODE     0x5350

#define MEL_SPEECH_ANDROID_DONE_OK       0
#define MEL_SPEECH_ANDROID_DONE_ABORTED  1
#define MEL_SPEECH_ANDROID_DONE_ERROR    2
#define MEL_SPEECH_ANDROID_DONE_DENIED   3
#define MEL_SPEECH_ANDROID_DONE_AUDIO    4
#define MEL_SPEECH_ANDROID_DONE_NETWORK  5

typedef struct
{
    u64             token;
    Mel_Speech_Sink sink;
} Droid_Job;

typedef Mel_Array(Droid_Job) Droid_Jobs;
typedef Mel_Array(str8) Droid_Strings;

typedef struct
{
    const Mel_Alloc* alloc;
    jclass           cls;
    bool             natives_registered;
    bool             perm_listening;
    Droid_Jobs       tts_jobs;
    Droid_Jobs       stt_jobs;
    Droid_Strings    voice_strings;
    Droid_Strings    rec_strings;
    Mel_Speech_Sink  auth_sink;
    bool             auth_pending;
} Droid;

static Droid g_droid;

static str8 droid_intern(Droid_Strings* strings, const char* utf8)
{
    usize len = utf8 ? strlen(utf8) : 0;
    u8*   data = (u8*)mel_alloc(g_droid.alloc, len + 1);
    if (!data)
        return (str8){ 0 };
    if (len)
        memcpy(data, utf8, len);
    data[len] = 0;
    str8 s = { data, (size)len };
    mel_array_push(strings, s);
    return s;
}

static void droid_strings_clear(Droid_Strings* strings)
{
    for (usize i = 0; i < strings->count; i++)
        if (strings->items[i].data)
            mel_dealloc(g_droid.alloc, strings->items[i].data);
    mel_array_clear(strings);
}

static Droid_Job* droid_job_find(Droid_Jobs* jobs, u64 token)
{
    for (usize i = 0; i < jobs->count; i++)
        if (jobs->items[i].token == token)
            return &jobs->items[i];
    return NULL;
}

static void droid_job_remove(Droid_Jobs* jobs, u64 token)
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

static Mel_Speech_Status droid_done_status(int code)
{
    switch (code)
    {
    case MEL_SPEECH_ANDROID_DONE_OK:
        return MEL_SPEECH_OK;
    case MEL_SPEECH_ANDROID_DONE_ABORTED:
        return MEL_SPEECH_OK | MEL_SPEECH_RESULT_ABORTED;
    case MEL_SPEECH_ANDROID_DONE_DENIED:
        return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_DENIED;
    case MEL_SPEECH_ANDROID_DONE_AUDIO:
        return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_AUDIO;
    case MEL_SPEECH_ANDROID_DONE_NETWORK:
        return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_NETWORK;
    default:
        return MEL_SPEECH_ERROR;
    }
}

static void JNICALL droid_native_tts_done(JNIEnv* env, jclass cls, jlong token, jint code)
{
    MEL_UNUSED(env);
    MEL_UNUSED(cls);
    Droid_Job* job = droid_job_find(&g_droid.tts_jobs, (u64)token);
    if (!job)
        return;
    Mel_Speech_Sink sink = job->sink;
    droid_job_remove(&g_droid.tts_jobs, (u64)token);
    if (sink.on_speak_done)
        sink.on_speak_done(sink.token, droid_done_status((int)code));
}

static void JNICALL droid_native_tts_range(JNIEnv* env, jclass cls, jlong token, jint offset, jint length)
{
    MEL_UNUSED(env);
    MEL_UNUSED(cls);
    Droid_Job* job = droid_job_find(&g_droid.tts_jobs, (u64)token);
    if (job && job->sink.on_range)
        job->sink.on_range(job->sink.token, (Mel_Speech_Range){ .offset = (usize)offset, .length = (usize)length });
}

static void JNICALL droid_native_stt_result(JNIEnv* env, jclass cls, jlong token, jstring text, jboolean is_final, jfloat confidence)
{
    MEL_UNUSED(cls);
    Droid_Job* job = droid_job_find(&g_droid.stt_jobs, (u64)token);
    if (!job || !job->sink.on_result || text == NULL)
        return;
    const char* utf8 = (*env)->GetStringUTFChars(env, text, NULL);
    if (!utf8)
        return;
    Mel_Speech_Result res = {
        .text = (str8){ (u8*)utf8, (size)strlen(utf8) },
        .final = is_final == JNI_TRUE,
        .confidence = (f32)confidence,
    };
    job->sink.on_result(job->sink.token, &res);
    (*env)->ReleaseStringUTFChars(env, text, utf8);
}

static void JNICALL droid_native_stt_done(JNIEnv* env, jclass cls, jlong token, jint code)
{
    MEL_UNUSED(env);
    MEL_UNUSED(cls);
    Droid_Job* job = droid_job_find(&g_droid.stt_jobs, (u64)token);
    if (!job)
        return;
    Mel_Speech_Sink sink = job->sink;
    droid_job_remove(&g_droid.stt_jobs, (u64)token);
    if (sink.on_listen_done)
        sink.on_listen_done(sink.token, droid_done_status((int)code));
}

static jclass droid_class(JNIEnv* env)
{
    if (g_droid.cls != NULL)
        return g_droid.cls;
    jclass cls = mel_platform_android_find_class(env, "orgwall/melody/speech/MelodySpeech");
    if (cls == NULL)
    {
        (*env)->ExceptionClear(env);
        mel_log_error("speech", "android: MelodySpeech class not found");
        return NULL;
    }
    if (!g_droid.natives_registered)
    {
        JNINativeMethod methods[] = {
            { "nativeTtsDone", "(JI)V", (void*)droid_native_tts_done },
            { "nativeTtsRange", "(JII)V", (void*)droid_native_tts_range },
            { "nativeSttResult", "(JLjava/lang/String;ZF)V", (void*)droid_native_stt_result },
            { "nativeSttDone", "(JI)V", (void*)droid_native_stt_done },
        };
        if ((*env)->RegisterNatives(env, cls, methods, 4) != 0)
        {
            (*env)->ExceptionClear(env);
            mel_log_error("speech", "android: RegisterNatives failed");
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
        return;
    }
    va_list args;
    va_start(args, sig);
    (*env)->CallStaticVoidMethodV(env, cls, m, args);
    va_end(args);
    if ((*env)->ExceptionCheck(env))
        (*env)->ExceptionClear(env);
}

static jstring droid_call_string(JNIEnv* env, jclass cls, const char* name, const char* sig, ...)
{
    jmethodID m = (*env)->GetStaticMethodID(env, cls, name, sig);
    if (!m)
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    va_list args;
    va_start(args, sig);
    jstring r = (jstring)(*env)->CallStaticObjectMethodV(env, cls, m, args);
    va_end(args);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    return r;
}

static str8 droid_intern_jstring(JNIEnv* env, Droid_Strings* strings, jstring js)
{
    if (js == NULL)
        return (str8){ 0 };
    const char* utf8 = (*env)->GetStringUTFChars(env, js, NULL);
    str8        s = droid_intern(strings, utf8 ? utf8 : "");
    if (utf8)
        (*env)->ReleaseStringUTFChars(env, js, utf8);
    (*env)->DeleteLocalRef(env, js);
    return s;
}

static void droid_ensure(const Mel_Alloc* alloc)
{
    if (g_droid.alloc != NULL)
        return;
    g_droid.alloc = alloc;
    mel_array_init(&g_droid.tts_jobs, alloc);
    mel_array_init(&g_droid.stt_jobs, alloc);
    mel_array_init(&g_droid.voice_strings, alloc);
    mel_array_init(&g_droid.rec_strings, alloc);
}

static u32 droid_enumerate_voices(void* user, const Mel_Alloc* alloc, Mel_Speech_Voice_Raw* out, u32 cap)
{
    MEL_UNUSED(user);
    droid_ensure(alloc);
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return 0;
    if ((*env)->PushLocalFrame(env, 32) != 0)
        return 0;
    jclass cls = droid_class(env);
    if (!cls)
    {
        (*env)->PopLocalFrame(env, NULL);
        return 0;
    }
    droid_call_void(env, cls, "ttsEnsure", "()V");
    u32 total = (u32)droid_call_int(env, cls, "voicesRefresh", "()I");
    u32 n = total < cap ? total : cap;
    droid_strings_clear(&g_droid.voice_strings);
    for (u32 i = 0; i < n; i++)
    {
        str8 name = droid_intern_jstring(env, &g_droid.voice_strings, droid_call_string(env, cls, "voiceName", "(I)Ljava/lang/String;", (jint)i));
        str8 lang = droid_intern_jstring(env, &g_droid.voice_strings, droid_call_string(env, cls, "voiceLang", "(I)Ljava/lang/String;", (jint)i));
        out[i] = (Mel_Speech_Voice_Raw){
            .stable_id = str8_hash(name),
            .name = name,
            .language = lang,
            .caps = {
                .rate = true,
                .rate_min = 0.25f,
                .rate_max = 4.0f,
                .pitch = true,
                .volume = true,
                .ranges = true,
                .can_pause = false,
            },
        };
    }
    (*env)->PopLocalFrame(env, NULL);
    return total;
}

static u32 droid_enumerate_recognizers(void* user, const Mel_Alloc* alloc, Mel_Speech_Recognizer_Raw* out, u32 cap)
{
    MEL_UNUSED(user);
    droid_ensure(alloc);
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return 0;
    if ((*env)->PushLocalFrame(env, 16) != 0)
        return 0;
    jclass cls = droid_class(env);
    if (!cls || !droid_call_bool(env, cls, "sttAvailable", "()Z"))
    {
        (*env)->PopLocalFrame(env, NULL);
        return 0;
    }
    if (cap >= 1)
    {
        droid_strings_clear(&g_droid.rec_strings);
        str8 lang = droid_intern_jstring(env, &g_droid.rec_strings, droid_call_string(env, cls, "defaultLanguage", "()Ljava/lang/String;"));
        out[0] = (Mel_Speech_Recognizer_Raw){
            .stable_id = MEL_SPEECH_ANDROID_RECOGNIZER_ID,
            .language = lang,
            .caps = {
                .on_device = false,
                .partials = true,
                .can_stop = true,
            },
        };
    }
    (*env)->PopLocalFrame(env, NULL);
    return 1;
}

static const str8* droid_voice_name_by_id(u64 stable_id)
{
    for (usize i = 0; i + 1 < g_droid.voice_strings.count; i += 2)
        if (str8_hash(g_droid.voice_strings.items[i]) == stable_id)
            return &g_droid.voice_strings.items[i];
    return NULL;
}

static Mel_Speech_Status droid_speak(void* user, u64 stable_id, u64 token, const Mel_Speech_Speak_Lowered* lowered, Mel_Speech_Sink sink)
{
    MEL_UNUSED(user);
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return MEL_SPEECH_ERROR;
    const str8* name = droid_voice_name_by_id(stable_id);
    if (!name)
    {
        mel_log_error("speech", "android speak: voice %llu not found", (unsigned long long)stable_id);
        return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_NO_DEVICE;
    }
    if ((*env)->PushLocalFrame(env, 16) != 0)
        return MEL_SPEECH_ERROR;
    jclass cls = droid_class(env);
    if (!cls)
    {
        (*env)->PopLocalFrame(env, NULL);
        return MEL_SPEECH_ERROR;
    }

    char* text_c = (char*)mel_alloc(g_droid.alloc, (usize)lowered->text.len + 1);
    if (!text_c)
    {
        (*env)->PopLocalFrame(env, NULL);
        return MEL_SPEECH_ERROR;
    }
    memcpy(text_c, lowered->text.data, (usize)lowered->text.len);
    text_c[lowered->text.len] = 0;
    jstring jtext = (*env)->NewStringUTF(env, text_c);
    mel_dealloc(g_droid.alloc, text_c);
    jstring jvoice = (*env)->NewStringUTF(env, (const char*)name->data);

    Droid_Job job = { .token = token, .sink = sink };
    mel_array_push(&g_droid.tts_jobs, job);
    bool ok = droid_call_bool(env, cls, "speak", "(Ljava/lang/String;Ljava/lang/String;FFFJ)Z", jtext, jvoice, (jfloat)lowered->rate, (jfloat)lowered->pitch, (jfloat)lowered->volume, (jlong)token);
    (*env)->PopLocalFrame(env, NULL);
    if (!ok)
    {
        droid_job_remove(&g_droid.tts_jobs, token);
        mel_log_error("speech", "android speak: engine not ready");
        return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_BUSY;
    }
    return MEL_SPEECH_OK;
}

static void droid_speak_abort(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    droid_job_remove(&g_droid.tts_jobs, token);
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return;
    if ((*env)->PushLocalFrame(env, 8) != 0)
        return;
    jclass cls = droid_class(env);
    if (cls)
        droid_call_void(env, cls, "ttsStop", "()V");
    (*env)->PopLocalFrame(env, NULL);
}

static const mel_speech_auth* droid_authorization(void* user)
{
    MEL_UNUSED(user);
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return &mel_speech_auth_not_determined;
    if ((*env)->PushLocalFrame(env, 8) != 0)
        return &mel_speech_auth_not_determined;
    jclass cls = droid_class(env);
    bool   granted = cls && droid_call_bool(env, cls, "micGranted", "()Z");
    (*env)->PopLocalFrame(env, NULL);
    return granted ? &mel_speech_auth_granted : &mel_speech_auth_not_determined;
}

static void droid_on_permission(void* user, i32 request_code, bool granted)
{
    MEL_UNUSED(user);
    if (request_code != MEL_SPEECH_ANDROID_PERM_CODE || !g_droid.auth_pending)
        return;
    g_droid.auth_pending = false;
    Mel_Speech_Sink sink = g_droid.auth_sink;
    if (sink.on_auth)
        sink.on_auth(sink.token, granted ? &mel_speech_auth_granted : &mel_speech_auth_denied);
}

static void droid_authorize(void* user, Mel_Speech_Sink sink)
{
    MEL_UNUSED(user);
    if (sink.on_auth == NULL)
        return;
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return;
    if ((*env)->PushLocalFrame(env, 8) != 0)
        return;
    jclass cls = droid_class(env);
    if (!cls)
    {
        (*env)->PopLocalFrame(env, NULL);
        sink.on_auth(sink.token, &mel_speech_auth_not_determined);
        return;
    }
    if (droid_call_bool(env, cls, "micGranted", "()Z"))
    {
        (*env)->PopLocalFrame(env, NULL);
        sink.on_auth(sink.token, &mel_speech_auth_granted);
        return;
    }
    if (!g_droid.perm_listening)
    {
        g_droid.perm_listening = true;
        mel_platform_android_permission_listen(g_droid.alloc, MEL_SPEECH_ANDROID_PERM_CODE, droid_on_permission, NULL);
    }
    g_droid.auth_sink = sink;
    g_droid.auth_pending = true;
    if (!droid_call_bool(env, cls, "requestMic", "()Z"))
    {
        g_droid.auth_pending = false;
        sink.on_auth(sink.token, &mel_speech_auth_not_determined);
    }
    (*env)->PopLocalFrame(env, NULL);
}

static Mel_Speech_Status droid_listen(void* user, u64 stable_id, u64 token, const Mel_Speech_Listen_Lowered* lowered, Mel_Speech_Sink sink)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return MEL_SPEECH_ERROR;
    if ((*env)->PushLocalFrame(env, 16) != 0)
        return MEL_SPEECH_ERROR;
    jclass cls = droid_class(env);
    if (!cls)
    {
        (*env)->PopLocalFrame(env, NULL);
        return MEL_SPEECH_ERROR;
    }
    if (!droid_call_bool(env, cls, "micGranted", "()Z"))
    {
        (*env)->PopLocalFrame(env, NULL);
        mel_log_error("speech", "android listen: RECORD_AUDIO not granted; call mel_speech_authorize first");
        return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_DENIED;
    }
    jstring   jlang = g_droid.rec_strings.count > 0 ? (*env)->NewStringUTF(env, (const char*)g_droid.rec_strings.items[0].data) : NULL;
    Droid_Job job = { .token = token, .sink = sink };
    mel_array_push(&g_droid.stt_jobs, job);
    bool ok = droid_call_bool(env, cls, "listenStart", "(JLjava/lang/String;Z)Z", (jlong)token, jlang, (jboolean)(lowered->partials ? JNI_TRUE : JNI_FALSE));
    (*env)->PopLocalFrame(env, NULL);
    if (!ok)
    {
        droid_job_remove(&g_droid.stt_jobs, token);
        mel_log_error("speech", "android listen: SpeechRecognizer unavailable");
        return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_UNSUPPORTED;
    }
    return MEL_SPEECH_OK;
}

static void droid_listen_simple(const char* method)
{
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return;
    if ((*env)->PushLocalFrame(env, 8) != 0)
        return;
    jclass cls = droid_class(env);
    if (cls)
        droid_call_void(env, cls, method, "()V");
    (*env)->PopLocalFrame(env, NULL);
}

static void droid_listen_stop(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    droid_listen_simple("listenStop");
}

static void droid_listen_abort(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    droid_job_remove(&g_droid.stt_jobs, token);
    droid_listen_simple("listenCancel");
}

static void droid_shutdown(void* user, const Mel_Alloc* alloc)
{
    MEL_UNUSED(user);
    MEL_UNUSED(alloc);
    if (g_droid.alloc == NULL)
        return;
    if (g_droid.perm_listening)
        mel_platform_android_permission_unlisten(MEL_SPEECH_ANDROID_PERM_CODE, droid_on_permission, NULL);
    droid_listen_simple("listenCancel");
    JNIEnv* env = mel_platform_android_env();
    if (env && g_droid.cls)
    {
        droid_call_void(env, g_droid.cls, "ttsStop", "()V");
        (*env)->DeleteGlobalRef(env, g_droid.cls);
    }
    droid_strings_clear(&g_droid.voice_strings);
    droid_strings_clear(&g_droid.rec_strings);
    mel_array_free(&g_droid.voice_strings);
    mel_array_free(&g_droid.rec_strings);
    mel_array_free(&g_droid.tts_jobs);
    mel_array_free(&g_droid.stt_jobs);
    memset(&g_droid, 0, sizeof g_droid);
}

void mel_speech__register_host_providers(void)
{
    static const Mel_Speech_Provider_Desc desc = {
        .name = "android-speech",
        .enumerate_voices = droid_enumerate_voices,
        .enumerate_recognizers = droid_enumerate_recognizers,
        .speak = droid_speak,
        .speak_abort = droid_speak_abort,
        .authorization = droid_authorization,
        .authorize = droid_authorize,
        .listen = droid_listen,
        .listen_stop = droid_listen_stop,
        .listen_abort = droid_listen_abort,
        .shutdown = droid_shutdown,
    };
    mel_speech_provider_register(&desc);
}
