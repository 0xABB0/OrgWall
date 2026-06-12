#include <core/platform.h>

#if !MEL_PLATFORM_ANDROID
#error "android-only translation unit"
#endif

#include <stt/provider.h>

#include <allocator/allocator.h>
#include <collection/array.h>
#include <string/str8.h>
#include <log/log.h>
#include <platform/android/jni.h>

#include <pthread.h>
#include <stdarg.h>
#include <string.h>

#define MEL_STT_ANDROID_RECOGNIZER_ID       0x616E64537474ull

#define MEL_STT_ANDROID_DONE_OK             0
#define MEL_STT_ANDROID_DONE_ERROR          1
#define MEL_STT_ANDROID_DONE_DENIED         2
#define MEL_STT_ANDROID_DONE_AUDIO          3
#define MEL_STT_ANDROID_DONE_NETWORK        4
#define MEL_STT_ANDROID_DONE_BUSY           5
#define MEL_STT_ANDROID_DONE_UNSUPPORTED    6

#define MEL_STT_ANDROID_ON_DEVICE_SUPPORTED 2

typedef struct
{
    u64          token;
    Mel_Stt_Sink sink;
} Stt_Job;

typedef Mel_Array(Stt_Job) Stt_Jobs;
typedef Mel_Array(str8) Stt_Strings;

typedef struct
{
    const Mel_Alloc* alloc;
    jclass           cls;
    bool             natives_registered;
    Stt_Jobs         jobs;
    Stt_Strings      strings;
    str8             language;
} Stt_Droid;

static Stt_Droid       g_stt;
static pthread_mutex_t g_stt_jobs_lock = PTHREAD_MUTEX_INITIALIZER;

static str8 stt_intern(Stt_Strings* strings, const char* utf8)
{
    usize len = utf8 ? strlen(utf8) : 0;
    u8*   data = (u8*)mel_alloc(g_stt.alloc, len + 1);
    if (!data)
        return (str8){ 0 };
    if (len)
        memcpy(data, utf8, len);
    data[len] = 0;
    str8 s = { data, (size)len };
    mel_array_push(strings, s);
    return s;
}

static void stt_strings_clear(Stt_Strings* strings)
{
    for (usize i = 0; i < strings->count; i++)
        if (strings->items[i].data)
            mel_dealloc(g_stt.alloc, strings->items[i].data);
    mel_array_clear(strings);
}

static bool stt_job_take(u64 token, bool remove, Mel_Stt_Sink* out)
{
    pthread_mutex_lock(&g_stt_jobs_lock);
    for (usize i = 0; i < g_stt.jobs.count; i++)
    {
        if (g_stt.jobs.items[i].token == token)
        {
            *out = g_stt.jobs.items[i].sink;
            if (remove)
                mel_array_remove_unordered(&g_stt.jobs, i);
            pthread_mutex_unlock(&g_stt_jobs_lock);
            return true;
        }
    }
    pthread_mutex_unlock(&g_stt_jobs_lock);
    return false;
}

static Mel_Stt_Status stt_done_status(int code)
{
    switch (code)
    {
    case MEL_STT_ANDROID_DONE_OK:
        return MEL_STT_OK;
    case MEL_STT_ANDROID_DONE_DENIED:
        return MEL_STT_ERROR | MEL_STT_RESULT_DENIED;
    case MEL_STT_ANDROID_DONE_AUDIO:
        return MEL_STT_ERROR | MEL_STT_RESULT_AUDIO;
    case MEL_STT_ANDROID_DONE_NETWORK:
        return MEL_STT_ERROR | MEL_STT_RESULT_NETWORK;
    case MEL_STT_ANDROID_DONE_BUSY:
        return MEL_STT_ERROR | MEL_STT_RESULT_BUSY;
    case MEL_STT_ANDROID_DONE_UNSUPPORTED:
        return MEL_STT_ERROR | MEL_STT_RESULT_UNSUPPORTED;
    default:
        return MEL_STT_ERROR;
    }
}

static void JNICALL stt_native_result(JNIEnv* env, jclass cls, jlong token, jstring text, jboolean is_final, jfloat confidence)
{
    MEL_UNUSED(cls);
    Mel_Stt_Sink sink;
    if (!stt_job_take((u64)token, false, &sink) || !sink.on_result || text == NULL)
        return;
    const char* utf8 = (*env)->GetStringUTFChars(env, text, NULL);
    if (!utf8)
        return;
    Mel_Stt_Result res = {
        .text = (str8){ (u8*)utf8, (size)strlen(utf8) },
        .final = is_final == JNI_TRUE,
        .confidence = (f32)confidence,
    };
    sink.on_result(sink.token, &res);
    (*env)->ReleaseStringUTFChars(env, text, utf8);
}

static void JNICALL stt_native_done(JNIEnv* env, jclass cls, jlong token, jint code)
{
    MEL_UNUSED(env);
    MEL_UNUSED(cls);
    Mel_Stt_Sink sink;
    if (!stt_job_take((u64)token, true, &sink))
        return;
    if (sink.on_done)
        sink.on_done(sink.token, stt_done_status((int)code));
}

static jclass stt_class(JNIEnv* env)
{
    if (g_stt.cls != NULL)
        return g_stt.cls;
    jclass cls = mel_platform_android_find_class(env, "orgwall/melody/stt/MelodyStt");
    if (cls == NULL)
    {
        (*env)->ExceptionClear(env);
        mel_log_error("stt", "android: MelodyStt class not found");
        return NULL;
    }
    if (!g_stt.natives_registered)
    {
        JNINativeMethod methods[] = {
            { "nativeResult", "(JLjava/lang/String;ZF)V", (void*)stt_native_result },
            { "nativeDone", "(JI)V", (void*)stt_native_done },
        };
        if ((*env)->RegisterNatives(env, cls, methods, 2) != 0)
        {
            (*env)->ExceptionClear(env);
            mel_log_error("stt", "android: RegisterNatives failed");
            return NULL;
        }
        g_stt.natives_registered = true;
    }
    g_stt.cls = (jclass)(*env)->NewGlobalRef(env, cls);
    return g_stt.cls;
}

static bool stt_call_bool(JNIEnv* env, jclass cls, const char* name, const char* sig, ...)
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

static i32 stt_call_int(JNIEnv* env, jclass cls, const char* name, const char* sig, ...)
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

static void stt_call_void(JNIEnv* env, jclass cls, const char* name, const char* sig, ...)
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

static jstring stt_call_string(JNIEnv* env, jclass cls, const char* name, const char* sig, ...)
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

static str8 stt_intern_jstring(JNIEnv* env, Stt_Strings* strings, jstring js)
{
    if (js == NULL)
        return (str8){ 0 };
    const char* utf8 = (*env)->GetStringUTFChars(env, js, NULL);
    str8        s = stt_intern(strings, utf8 ? utf8 : "");
    if (utf8)
        (*env)->ReleaseStringUTFChars(env, js, utf8);
    (*env)->DeleteLocalRef(env, js);
    return s;
}

static jstring stt_jstring(JNIEnv* env, str8 s)
{
    char* c = (char*)mel_alloc(g_stt.alloc, (usize)s.len + 1);
    if (!c)
        return NULL;
    if (s.len)
        memcpy(c, s.data, (usize)s.len);
    c[s.len] = 0;
    jstring js = (*env)->NewStringUTF(env, c);
    mel_dealloc(g_stt.alloc, c);
    return js;
}

static void stt_ensure(const Mel_Alloc* alloc)
{
    if (g_stt.alloc != NULL)
        return;
    g_stt.alloc = alloc;
    mel_array_init(&g_stt.jobs, alloc);
    mel_array_init(&g_stt.strings, alloc);
}

static u32 stt_enumerate(void* user, const Mel_Alloc* alloc, Mel_Stt_Recognizer_Raw* out, u32 cap)
{
    MEL_UNUSED(user);
    stt_ensure(alloc);
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return 0;
    if ((*env)->PushLocalFrame(env, 16) != 0)
        return 0;
    jclass cls = stt_class(env);
    if (!cls || !stt_call_bool(env, cls, "available", "()Z"))
    {
        (*env)->PopLocalFrame(env, NULL);
        return 0;
    }
    bool on_device = stt_call_int(env, cls, "onDeviceSupport", "()I") == MEL_STT_ANDROID_ON_DEVICE_SUPPORTED;
    bool biasing = stt_call_bool(env, cls, "biasingSupported", "()Z");
    if (cap >= 1)
    {
        stt_strings_clear(&g_stt.strings);
        g_stt.language = stt_intern_jstring(env, &g_stt.strings, stt_call_string(env, cls, "defaultLanguage", "()Ljava/lang/String;"));
        out[0] = (Mel_Stt_Recognizer_Raw){
            .stable_id = MEL_STT_ANDROID_RECOGNIZER_ID,
            .language = g_stt.language,
            .caps = {
                .on_device = on_device,
                .require_on_device = on_device,
                .partials = true,
                .can_stop = true,
                .feed = false,
                .device_select = false,
                .vocabulary = biasing,
                .punctuation = false,
                .profanity_filter = false,
            },
        };
    }
    (*env)->PopLocalFrame(env, NULL);
    return 1;
}

static const mel_stt_auth* stt_authorization(void* user)
{
    MEL_UNUSED(user);
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return &mel_stt_auth_restricted;
    if ((*env)->PushLocalFrame(env, 8) != 0)
        return &mel_stt_auth_restricted;
    jclass cls = stt_class(env);
    bool   available = cls && stt_call_bool(env, cls, "available", "()Z");
    (*env)->PopLocalFrame(env, NULL);
    return available ? &mel_stt_auth_granted : &mel_stt_auth_restricted;
}

static void stt_authorize(void* user, Mel_Stt_Sink sink)
{
    if (sink.on_auth)
        sink.on_auth(sink.token, stt_authorization(user));
}

static Mel_Stt_Status stt_listen(void* user, u64 stable_id, u64 token, const Mel_Stt_Listen_Lowered* lowered, Mel_Stt_Sink sink)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return MEL_STT_ERROR;
    if ((*env)->PushLocalFrame(env, 32) != 0)
        return MEL_STT_ERROR;
    jclass cls = stt_class(env);
    if (!cls)
    {
        (*env)->PopLocalFrame(env, NULL);
        return MEL_STT_ERROR;
    }

    jstring      jlang = g_stt.language.len > 0 ? stt_jstring(env, g_stt.language) : NULL;
    jobjectArray jbias = NULL;
    if (lowered->vocabulary_count > 0)
    {
        jclass str_cls = (*env)->FindClass(env, "java/lang/String");
        if (str_cls)
            jbias = (*env)->NewObjectArray(env, (jsize)lowered->vocabulary_count, str_cls, NULL);
        if (!jbias)
        {
            (*env)->ExceptionClear(env);
            (*env)->PopLocalFrame(env, NULL);
            mel_log_error("stt", "android listen: biasing array allocation failed");
            return MEL_STT_ERROR;
        }
        for (u32 i = 0; i < lowered->vocabulary_count; i++)
        {
            jstring js = stt_jstring(env, lowered->vocabulary[i]);
            if (!js)
            {
                (*env)->PopLocalFrame(env, NULL);
                mel_log_error("stt", "android listen: biasing string allocation failed");
                return MEL_STT_ERROR;
            }
            (*env)->SetObjectArrayElement(env, jbias, (jsize)i, js);
            (*env)->DeleteLocalRef(env, js);
        }
    }

    pthread_mutex_lock(&g_stt_jobs_lock);
    Stt_Job job = { .token = token, .sink = sink };
    mel_array_push(&g_stt.jobs, job);
    pthread_mutex_unlock(&g_stt_jobs_lock);

    bool ok = stt_call_bool(env,
                            cls,
                            "listenStart",
                            "(JLjava/lang/String;ZZ[Ljava/lang/String;)Z",
                            (jlong)token,
                            jlang,
                            (jboolean)(lowered->partials ? JNI_TRUE : JNI_FALSE),
                            (jboolean)(lowered->require_on_device ? JNI_TRUE : JNI_FALSE),
                            jbias);
    (*env)->PopLocalFrame(env, NULL);
    if (!ok)
    {
        Mel_Stt_Sink dropped;
        stt_job_take(token, true, &dropped);
        mel_log_error("stt", "android listen: SpeechRecognizer unavailable");
        return MEL_STT_ERROR | MEL_STT_RESULT_UNSUPPORTED;
    }
    return MEL_STT_OK;
}

static void stt_simple(const char* method)
{
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return;
    if ((*env)->PushLocalFrame(env, 8) != 0)
        return;
    jclass cls = stt_class(env);
    if (cls)
        stt_call_void(env, cls, method, "()V");
    (*env)->PopLocalFrame(env, NULL);
}

static void stt_stop(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    MEL_UNUSED(token);
    stt_simple("listenStop");
}

static void stt_abort(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    Mel_Stt_Sink dropped;
    stt_job_take(token, true, &dropped);
    stt_simple("listenCancel");
}

static void stt_shutdown(void* user, const Mel_Alloc* alloc)
{
    MEL_UNUSED(user);
    MEL_UNUSED(alloc);
    if (g_stt.alloc == NULL)
        return;
    stt_simple("shutdown");
    JNIEnv* env = mel_platform_android_env();
    if (env && g_stt.cls)
        (*env)->DeleteGlobalRef(env, g_stt.cls);
    stt_strings_clear(&g_stt.strings);
    mel_array_free(&g_stt.strings);
    pthread_mutex_lock(&g_stt_jobs_lock);
    mel_array_free(&g_stt.jobs);
    pthread_mutex_unlock(&g_stt_jobs_lock);
    memset(&g_stt, 0, sizeof g_stt);
}

void mel_stt__register_host_providers(void)
{
    static const Mel_Stt_Provider_Desc desc = {
        .name = "android-speechrecognizer",
        .enumerate_recognizers = stt_enumerate,
        .authorization = stt_authorization,
        .authorize = stt_authorize,
        .listen = stt_listen,
        .stop = stt_stop,
        .abort = stt_abort,
        .feed = NULL,
        .recognizer_native = NULL,
        .shutdown = stt_shutdown,
    };
    mel_stt_provider_register(&desc);
}
