#include <core/platform.h>

#if !MEL_PLATFORM_ANDROID
#error "android-only translation unit"
#endif

#include <platform/platform.h>
#include <platform/android/android.h>
#include "../../src/platform_internal.h"

#include <allocator/allocator.h>
#include <collection/array.h>
#include <executor/executor.h>

#include <stdatomic.h>
#include <string.h>

static jobject g_activity;

void mel_platform_android_set_activity(jobject activity)
{
    JNIEnv* env = mel_platform_android_env();
    if (env == NULL)
        return;
    if (g_activity)
        (*env)->DeleteGlobalRef(env, g_activity);
    g_activity = activity ? (*env)->NewGlobalRef(env, activity) : NULL;
}

jobject mel_platform_android_activity(void) { return g_activity; }

static jclass find_helper(JNIEnv* env) { return (*env)->FindClass(env, "orgwall/melody/platform/MelodyPlatform"); }

i32 mel_platform_android_sdk_version(void)
{
    JNIEnv* env = mel_platform_android_env();
    if (env == NULL)
        return 0;
    jclass build = (*env)->FindClass(env, "android/os/Build$VERSION");
    if (build == NULL)
        return 0;
    jfieldID sdk = (*env)->GetStaticFieldID(env, build, "SDK_INT", "I");
    return (i32)(*env)->GetStaticIntField(env, build, sdk);
}

static const char* android_name(void) { return "android"; }

static u32 android_device_class(void)
{
    JNIEnv* env = mel_platform_android_env();
    if (env == NULL || g_activity == NULL)
        return MEL_PLATFORM_DEVICE_UNKNOWN;
    jclass helper = find_helper(env);
    if (helper == NULL)
        return MEL_PLATFORM_DEVICE_UNKNOWN;
    jmethodID m = (*env)->GetStaticMethodID(env, helper, "deviceClass", "(Landroid/app/Activity;)I");
    if (m == NULL)
        return MEL_PLATFORM_DEVICE_UNKNOWN;
    return (u32)(*env)->CallStaticIntMethod(env, helper, m, g_activity);
}

static Mel_Platform_Sandbox android_sandbox(void) { return (Mel_Platform_Sandbox){ MEL_PLATFORM_SANDBOX_NONE, NULL }; }

static Mel_Platform_Inhibit_Native android_inhibit(const char* reason)
{
    (void)reason;
    JNIEnv* env = mel_platform_android_env();
    if (env == NULL || g_activity == NULL)
        return (Mel_Platform_Inhibit_Native){ MEL_PLATFORM_ERROR | MEL_PLATFORM_UNAVAILABLE, 0 };
    jclass helper = find_helper(env);
    if (helper == NULL)
        return (Mel_Platform_Inhibit_Native){ MEL_PLATFORM_ERROR | MEL_PLATFORM_UNAVAILABLE, 0 };
    jmethodID m = (*env)->GetStaticMethodID(env, helper, "setKeepScreenOn", "(Landroid/app/Activity;Z)V");
    if (m == NULL)
        return (Mel_Platform_Inhibit_Native){ MEL_PLATFORM_ERROR | MEL_PLATFORM_UNAVAILABLE, 0 };
    (*env)->CallStaticVoidMethod(env, helper, m, g_activity, JNI_TRUE);
    return (Mel_Platform_Inhibit_Native){ MEL_PLATFORM_OK, 1 };
}

static Mel_Platform_Status android_uninhibit(u64 native)
{
    (void)native;
    JNIEnv* env = mel_platform_android_env();
    if (env == NULL || g_activity == NULL)
        return MEL_PLATFORM_ERROR | MEL_PLATFORM_UNAVAILABLE;
    jclass helper = find_helper(env);
    if (helper == NULL)
        return MEL_PLATFORM_ERROR | MEL_PLATFORM_UNAVAILABLE;
    jmethodID m = (*env)->GetStaticMethodID(env, helper, "setKeepScreenOn", "(Landroid/app/Activity;Z)V");
    if (m == NULL)
        return MEL_PLATFORM_ERROR | MEL_PLATFORM_UNAVAILABLE;
    (*env)->CallStaticVoidMethod(env, helper, m, g_activity, JNI_FALSE);
    return MEL_PLATFORM_OK;
}

const Mel_Platform_Backend* mel_platform__backend(void)
{
    static const Mel_Platform_Backend backend = {
        .name = android_name,
        .device_class = android_device_class,
        .sandbox = android_sandbox,
        .screensaver_inhibit = android_inhibit,
        .screensaver_uninhibit = android_uninhibit,
    };
    return &backend;
}

Mel_Platform_Status mel_platform_android_toast(const char* text, u32 flags)
{
    JNIEnv* env = mel_platform_android_env();
    if (env == NULL || g_activity == NULL || text == NULL)
        return MEL_PLATFORM_ERROR | MEL_PLATFORM_UNAVAILABLE;
    jclass helper = find_helper(env);
    if (helper == NULL)
        return MEL_PLATFORM_ERROR | MEL_PLATFORM_UNAVAILABLE;
    jmethodID m = (*env)->GetStaticMethodID(env, helper, "toast", "(Landroid/app/Activity;Ljava/lang/String;Z)V");
    if (m == NULL)
        return MEL_PLATFORM_ERROR | MEL_PLATFORM_UNAVAILABLE;
    jstring js = (*env)->NewStringUTF(env, text);
    (*env)->CallStaticVoidMethod(env, helper, m, g_activity, js, (flags & MEL_PLATFORM_TOAST_LONG) ? JNI_TRUE : JNI_FALSE);
    (*env)->DeleteLocalRef(env, js);
    return MEL_PLATFORM_OK;
}

static char* g_internal_path;
static char* g_external_path;
static char* g_cache_path;

static const char* fetch_path(const char* method, char** cache)
{
    if (*cache)
        return *cache;
    JNIEnv* env = mel_platform_android_env();
    if (env == NULL || g_activity == NULL)
        return NULL;
    jclass helper = find_helper(env);
    if (helper == NULL)
        return NULL;
    jmethodID m = (*env)->GetStaticMethodID(env, helper, method, "(Landroid/app/Activity;)Ljava/lang/String;");
    if (m == NULL)
        return NULL;
    jstring js = (jstring)(*env)->CallStaticObjectMethod(env, helper, m, g_activity);
    if (js == NULL)
        return NULL;
    const char*      utf = (*env)->GetStringUTFChars(env, js, NULL);
    const Mel_Alloc* alloc = mel_platform__alloc();
    usize            len = (usize)(*env)->GetStringUTFLength(env, js);
    char*            copy = mel_alloc(alloc, len + 1);
    memcpy(copy, utf, len);
    copy[len] = '\0';
    (*env)->ReleaseStringUTFChars(env, js, utf);
    (*env)->DeleteLocalRef(env, js);
    *cache = copy;
    return copy;
}

const char* mel_platform_android_internal_storage_path(void) { return fetch_path("internalStoragePath", &g_internal_path); }
const char* mel_platform_android_external_storage_path(void) { return fetch_path("externalStoragePath", &g_external_path); }
const char* mel_platform_android_cache_path(void) { return fetch_path("cachePath", &g_cache_path); }

typedef struct
{
    u64           token;
    Mel_Future*   future;
    Mel_Executor* deliver;
} Pending_Permission;

static Mel_Array(Pending_Permission) g_pending;
static bool         g_pending_init;
static _Atomic(u64) g_token_seq;

static void permission_free(void* value, const Mel_Alloc* alloc) { mel_dealloc(alloc, value); }

void mel_platform_android_permission_free(Mel_Future* f)
{
    if (f == NULL)
        return;
    const Mel_Alloc* alloc = f->alloc;
    if (f->free_value != NULL && f->value != NULL)
        f->free_value(f->value, alloc);
    mel_dealloc(alloc, f);
}

Mel_Future* mel_platform_android_request_permission_opt(const char* permission, Mel_Platform_Permission_Opt opt)
{
    const Mel_Alloc* alloc = mel_platform__alloc();
    Mel_Future*      f = mel_alloc_type(alloc, Mel_Future);
    mel_future_init(f, permission_free, alloc);

    JNIEnv* env = mel_platform_android_env();
    if (env == NULL || g_activity == NULL || permission == NULL)
    {
        Mel_Platform_Permission_Outcome* out = mel_alloc_type(alloc, Mel_Platform_Permission_Outcome);
        out->result = MEL_PLATFORM_PERMISSION_DENIED;
        mel_future_resolve(f, out, MEL_FUTURE_ERROR | MEL_FUTURE_BROKEN);
        return f;
    }

    if (!g_pending_init)
    {
        mel_array_init(&g_pending, alloc);
        g_pending_init = true;
    }
    u64                token = atomic_fetch_add(&g_token_seq, 1) + 1;
    Pending_Permission p = { token, f, opt.deliver };
    mel_array_push(&g_pending, p);

    jclass    helper = find_helper(env);
    jmethodID m = (*env)->GetStaticMethodID(env, helper, "requestPermission", "(Landroid/app/Activity;Ljava/lang/String;J)V");
    jstring   js = (*env)->NewStringUTF(env, permission);
    (*env)->CallStaticVoidMethod(env, helper, m, g_activity, js, (jlong)token);
    (*env)->DeleteLocalRef(env, js);

    return f;
}

const Mel_Platform_Permission_Outcome* mel_platform_android_permission_outcome(Mel_Future* f)
{
    if (f == NULL || !mel_future_resolved(f))
        return NULL;
    return (const Mel_Platform_Permission_Outcome*)mel_future_value(f);
}

typedef struct
{
    Mel_Future* future;
    bool        granted;
} Permission_Resolution;

static void permission_resolution_run(void* data)
{
    Permission_Resolution*           r = (Permission_Resolution*)data;
    const Mel_Alloc*                 alloc = r->future->alloc;
    Mel_Platform_Permission_Outcome* out = mel_alloc_type(alloc, Mel_Platform_Permission_Outcome);
    out->result = r->granted ? MEL_PLATFORM_PERMISSION_GRANTED : MEL_PLATFORM_PERMISSION_DENIED;
    mel_future_resolve(r->future, out, MEL_FUTURE_OK);
    mel_dealloc(alloc, r);
}

void mel_platform_android__permission_resolve(u64 token, bool granted)
{
    for (usize i = 0; i < g_pending.count; i++)
    {
        if (g_pending.items[i].token != token)
            continue;
        Mel_Future*      f = g_pending.items[i].future;
        Mel_Executor*    deliver = g_pending.items[i].deliver;
        const Mel_Alloc* alloc = f->alloc;
        mel_array_remove_unordered(&g_pending, i);
        Permission_Resolution* r = mel_alloc_type(alloc, Permission_Resolution);
        r->future = f;
        r->granted = granted;
        mel_executor_call(deliver ? deliver : mel_executor_inline(), permission_resolution_run, r, alloc);
        return;
    }
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_MelodyPlatform_nativePermissionResult(JNIEnv* env, jclass clazz, jlong token, jboolean granted)
{
    (void)env;
    (void)clazz;
    mel_platform_android__permission_resolve((u64)token, granted == JNI_TRUE);
}
