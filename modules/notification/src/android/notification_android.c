#include <notification/notification.h>
#include <notification/provider.h>

#include <platform/android/jni.h>
#include <allocator/allocator.h>
#include <log/log.h>

#include <string.h>
#include <time.h>

#include "../notification_internal.h"

#define MEL_NOTIF_PERMISSION_REQUEST_CODE 0x4D4E

#define HELPER_CLASS   "orgwall/melody/notification/MelodyNotification"
#define RECEIVER_CLASS "orgwall/melody/notification/MelodyNotificationReceiver"

typedef struct
{
    Mel_Notif_Sink sink;
    bool           sink_pending;
    bool           listening;
    bool           natives_registered;
} Android_State;

static Android_State ax;

static u64 now_unix_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (u64)ts.tv_sec * 1000u + (u64)ts.tv_nsec / 1000000u;
}

static jobject android_context(JNIEnv* env)
{
    jclass at = (*env)->FindClass(env, "android/app/ActivityThread");
    if (at == NULL)
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    jmethodID cur = (*env)->GetStaticMethodID(env, at, "currentApplication", "()Landroid/app/Application;");
    if (cur == NULL)
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    jobject app = (*env)->CallStaticObjectMethod(env, at, cur);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    return app;
}

static jclass helper_class(JNIEnv* env)
{
    jclass cls = mel_platform_android_find_class(env, HELPER_CLASS);
    if (cls == NULL)
    {
        (*env)->ExceptionClear(env);
        mel_log_error("notification", "android: MelodyNotification helper class not found");
    }
    return cls;
}

static jstring jstr(JNIEnv* env, str8 s)
{
    if (s.len == 0 || s.data == NULL)
        return NULL;
    const Mel_Alloc* a = mel_notif__alloc();
    char*            c = (char*)mel_alloc(a, (usize)s.len + 1);
    if (c == NULL)
        return NULL;
    memcpy(c, s.data, (usize)s.len);
    c[s.len] = 0;
    jstring j = (*env)->NewStringUTF(env, c);
    mel_dealloc(a, c);
    return j;
}

static jintArray argb_array(JNIEnv* env, const Mel_Notif_Image* img)
{
    if (img->rgba == NULL || img->width == 0 || img->height == 0)
        return NULL;
    usize     n = (usize)img->width * (usize)img->height;
    jintArray arr = (*env)->NewIntArray(env, (jsize)n);
    if (arr == NULL)
        return NULL;
    jint* tmp = (jint*)mel_alloc(mel_notif__alloc(), n * sizeof(jint));
    if (tmp == NULL)
        return NULL;
    for (usize i = 0; i < n; i++)
    {
        const u8* p = img->rgba + i * 4;
        tmp[i] = (jint)(((u32)p[3] << 24) | ((u32)p[0] << 16) | ((u32)p[1] << 8) | (u32)p[2]);
    }
    (*env)->SetIntArrayRegion(env, arr, 0, (jsize)n, tmp);
    mel_dealloc(mel_notif__alloc(), tmp);
    return arr;
}

static void JNICALL native_event(JNIEnv* env, jclass cls, jlong token, jstring action, jstring reply, jboolean dismissed)
{
    MEL_UNUSED(cls);
    if (dismissed)
    {
        mel_notif__dispatch_dismissed((u64)token);
        return;
    }
    const char* action_c = action != NULL ? (*env)->GetStringUTFChars(env, action, NULL) : NULL;
    const char* reply_c = reply != NULL ? (*env)->GetStringUTFChars(env, reply, NULL) : NULL;
    str8        action_s = (action_c != NULL && action_c[0] != 0) ? (str8){ (u8*)action_c, (size)strlen(action_c) } : STR8_EMPTY;
    str8        reply_s = (reply_c != NULL && reply_c[0] != 0) ? (str8){ (u8*)reply_c, (size)strlen(reply_c) } : STR8_EMPTY;
    mel_notif__dispatch_activated((u64)token, action_s, reply_s, STR8_EMPTY);
    if (action_c != NULL)
        (*env)->ReleaseStringUTFChars(env, action, action_c);
    if (reply_c != NULL)
        (*env)->ReleaseStringUTFChars(env, reply, reply_c);
}

static void register_natives(JNIEnv* env)
{
    if (ax.natives_registered)
        return;
    jclass receiver = mel_platform_android_find_class(env, RECEIVER_CLASS);
    if (receiver == NULL)
    {
        (*env)->ExceptionClear(env);
        return;
    }
    JNINativeMethod methods[] = {
        { "nativeEvent", "(JLjava/lang/String;Ljava/lang/String;Z)V", (void*)native_event },
    };
    if ((*env)->RegisterNatives(env, receiver, methods, 1) != 0)
    {
        (*env)->ExceptionClear(env);
        mel_log_error("notification", "android: RegisterNatives failed for receiver");
        return;
    }
    jfieldID ready = (*env)->GetStaticFieldID(env, receiver, "nativeReady", "Z");
    if (ready != NULL)
        (*env)->SetStaticBooleanField(env, receiver, ready, JNI_TRUE);
    else
        (*env)->ExceptionClear(env);
    ax.natives_registered = true;
}

static bool call_bool(const char* method, const char* sig_with_ctx)
{
    JNIEnv* env = mel_platform_android_env();
    if (env == NULL || (*env)->PushLocalFrame(env, 8) != 0)
        return false;
    jobject ctx = android_context(env);
    jclass  cls = ctx != NULL ? helper_class(env) : NULL;
    bool    out = false;
    if (cls != NULL)
    {
        jmethodID m = (*env)->GetStaticMethodID(env, cls, method, sig_with_ctx);
        if (m != NULL)
            out = (*env)->CallStaticBooleanMethod(env, cls, m, ctx) == JNI_TRUE;
        else
            (*env)->ExceptionClear(env);
        if ((*env)->ExceptionCheck(env))
        {
            (*env)->ExceptionClear(env);
            out = false;
        }
    }
    (*env)->PopLocalFrame(env, NULL);
    return out;
}

static bool android_supported(void* user)
{
    MEL_UNUSED(user);
    JNIEnv* env = mel_platform_android_env();
    if (env == NULL)
        return false;
    if ((*env)->PushLocalFrame(env, 8) != 0)
        return false;
    bool ok = android_context(env) != NULL && helper_class(env) != NULL;
    (*env)->PopLocalFrame(env, NULL);
    return ok;
}

static Mel_Notif_Caps android_caps(void* user)
{
    MEL_UNUSED(user);
    return MEL_NOTIF_CAP_ACTIONS | MEL_NOTIF_CAP_REPLY | MEL_NOTIF_CAP_ICON | MEL_NOTIF_CAP_ATTACHMENT | MEL_NOTIF_CAP_PROGRESS | MEL_NOTIF_CAP_BADGE | MEL_NOTIF_CAP_SCHEDULE | MEL_NOTIF_CAP_REPEAT | MEL_NOTIF_CAP_UPDATE | MEL_NOTIF_CAP_CHANNELS | MEL_NOTIF_CAP_AUTH;
}

static const mel_notif_auth* android_authorization(void* user)
{
    MEL_UNUSED(user);
    if (call_bool("enabled", "(Landroid/content/Context;)Z"))
        return &mel_notif_auth_granted;
    if (call_bool("needsRuntimePermission", "(Landroid/content/Context;)Z"))
        return &mel_notif_auth_not_determined;
    return &mel_notif_auth_denied;
}

static void on_permission_result(void* user, i32 request_code, bool granted)
{
    MEL_UNUSED(user);
    if (request_code != MEL_NOTIF_PERMISSION_REQUEST_CODE || !ax.sink_pending)
        return;
    ax.sink_pending = false;
    const mel_notif_auth* a = granted ? &mel_notif_auth_granted : &mel_notif_auth_denied;
    ax.sink.on_auth(ax.sink.token, a);
}

static void android_authorize(void* user, Mel_Notif_Sink sink)
{
    MEL_UNUSED(user);
    if (!call_bool("needsRuntimePermission", "(Landroid/content/Context;)Z"))
    {
        sink.on_auth(sink.token, android_authorization(NULL));
        return;
    }
    if (!ax.listening)
    {
        mel_platform_android_permission_listen(mel_notif__alloc(), MEL_NOTIF_PERMISSION_REQUEST_CODE, on_permission_result, NULL);
        ax.listening = true;
    }
    ax.sink = sink;
    ax.sink_pending = true;

    JNIEnv* env = mel_platform_android_env();
    bool    requested = false;
    if (env != NULL && (*env)->PushLocalFrame(env, 8) == 0)
    {
        jclass cls = helper_class(env);
        if (cls != NULL)
        {
            jmethodID m = (*env)->GetStaticMethodID(env, cls, "requestPermission", "()Z");
            if (m != NULL)
                requested = (*env)->CallStaticBooleanMethod(env, cls, m) == JNI_TRUE;
            else
                (*env)->ExceptionClear(env);
        }
        (*env)->PopLocalFrame(env, NULL);
    }
    if (!requested)
    {
        ax.sink_pending = false;
        mel_log_error("notification", "android: cannot request POST_NOTIFICATIONS (no resumed activity)");
        sink.on_auth(sink.token, &mel_notif_auth_not_determined);
    }
}

static Mel_Notif_Status android_channel_register(void* user, const Mel_Notif_Channel_Opt* opt)
{
    MEL_UNUSED(user);
    JNIEnv* env = mel_platform_android_env();
    if (env == NULL || (*env)->PushLocalFrame(env, 16) != 0)
        return MEL_NOTIF_ERROR | MEL_NOTIF_ERR_BACKEND_FAIL;
    jobject          ctx = android_context(env);
    jclass           cls = ctx != NULL ? helper_class(env) : NULL;
    Mel_Notif_Status s = MEL_NOTIF_ERROR | MEL_NOTIF_ERR_BACKEND_FAIL;
    if (cls != NULL)
    {
        jmethodID m = (*env)->GetStaticMethodID(env, cls, "ensureChannel", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;ZZ)V");
        if (m != NULL)
        {
            (*env)->CallStaticVoidMethod(env, cls, m, ctx, jstr(env, opt->id), jstr(env, opt->label), jstr(env, opt->description), (jboolean)opt->high, (jboolean)opt->silent);
            if ((*env)->ExceptionCheck(env))
                (*env)->ExceptionClear(env);
            else
                s = MEL_NOTIF_OK;
        }
        else
            (*env)->ExceptionClear(env);
    }
    (*env)->PopLocalFrame(env, NULL);
    return s;
}

static Mel_Notif_Status android_post(void* user, const Mel_Notif_Lowered* lw)
{
    MEL_UNUSED(user);
    const Mel_Notif_Content* c = lw->content;
    JNIEnv*                  env = mel_platform_android_env();
    if (env == NULL || (*env)->PushLocalFrame(env, 64) != 0)
        return MEL_NOTIF_ERROR | MEL_NOTIF_ERR_BACKEND_FAIL;
    jobject ctx = android_context(env);
    jclass  cls = ctx != NULL ? helper_class(env) : NULL;
    if (cls == NULL)
    {
        (*env)->PopLocalFrame(env, NULL);
        return MEL_NOTIF_ERROR | MEL_NOTIF_ERR_BACKEND_FAIL;
    }
    register_natives(env);

    jmethodID post = (*env)->GetStaticMethodID(env, cls, "post",
                                               "(Landroid/content/Context;JLjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;[IIILjava/lang/String;[IIILjava/lang/String;[Ljava/lang/String;[Ljava/lang/String;[IZZIZIJJ)V");
    if (post == NULL)
    {
        (*env)->ExceptionClear(env);
        (*env)->PopLocalFrame(env, NULL);
        mel_log_error("notification", "android: MelodyNotification.post not found");
        return MEL_NOTIF_ERROR | MEL_NOTIF_ERR_BACKEND_FAIL;
    }

    Mel_Notif_Status warn = 0;
    if (c->sound_path.len > 0)
        warn |= MEL_NOTIF_WARN_SOUND_DROPPED;

    jobjectArray action_ids = NULL;
    jobjectArray action_labels = NULL;
    jintArray    action_flags = NULL;
    if (c->action_count > 0)
    {
        jclass string_cls = (*env)->FindClass(env, "java/lang/String");
        action_ids = (*env)->NewObjectArray(env, (jsize)c->action_count, string_cls, NULL);
        action_labels = (*env)->NewObjectArray(env, (jsize)c->action_count, string_cls, NULL);
        action_flags = (*env)->NewIntArray(env, (jsize)c->action_count);
        for (u32 i = 0; i < c->action_count; i++)
        {
            (*env)->SetObjectArrayElement(env, action_ids, (jsize)i, jstr(env, c->actions[i].id));
            (*env)->SetObjectArrayElement(env, action_labels, (jsize)i, jstr(env, c->actions[i].label));
            jint f = (jint)c->actions[i].flags;
            (*env)->SetIntArrayRegion(env, action_flags, (jsize)i, 1, &f);
        }
    }

    jlong delay_ms = 0;
    jlong interval_ms = 0;
    if (lw->scheduled)
    {
        u64 now = now_unix_ms();
        if (lw->trigger.at_unix_ms > now)
            delay_ms = (jlong)(lw->trigger.at_unix_ms - now);
        else if (lw->trigger.at_unix_ms > 0)
            delay_ms = 1;
        interval_ms = (jlong)lw->trigger.interval_ms;
    }

    i32 progress_pct = c->progress.value <= 0.0f ? 0 : (c->progress.value >= 1.0f ? 100 : (i32)(c->progress.value * 100.0f));

    (*env)->CallStaticVoidMethod(env, cls, post, ctx, (jlong)lw->token,
                                 jstr(env, c->channel), jstr(env, c->title), jstr(env, c->subtitle), jstr(env, c->body), jstr(env, c->group),
                                 argb_array(env, &c->icon), (jint)c->icon.width, (jint)c->icon.height, jstr(env, c->icon.path),
                                 argb_array(env, &c->attachment), (jint)c->attachment.width, (jint)c->attachment.height, jstr(env, c->attachment.path),
                                 action_ids, action_labels, action_flags,
                                 (jboolean)c->progress.present, (jboolean)c->progress.indeterminate, (jint)progress_pct,
                                 (jboolean)c->has_badge, (jint)c->badge,
                                 delay_ms, interval_ms);
    bool failed = (*env)->ExceptionCheck(env);
    if (failed)
    {
        (*env)->ExceptionClear(env);
        mel_log_error("notification", "android: post threw");
    }
    (*env)->PopLocalFrame(env, NULL);
    if (failed)
        return MEL_NOTIF_ERROR | MEL_NOTIF_ERR_BACKEND_FAIL;
    return warn != 0 ? (MEL_NOTIF_WARNED | warn) : MEL_NOTIF_OK;
}

static void android_cancel(void* user, u64 token)
{
    MEL_UNUSED(user);
    JNIEnv* env = mel_platform_android_env();
    if (env == NULL || (*env)->PushLocalFrame(env, 8) != 0)
        return;
    jobject ctx = android_context(env);
    jclass  cls = ctx != NULL ? helper_class(env) : NULL;
    if (cls != NULL)
    {
        jmethodID m = (*env)->GetStaticMethodID(env, cls, "cancel", "(Landroid/content/Context;J)V");
        if (m != NULL)
            (*env)->CallStaticVoidMethod(env, cls, m, ctx, (jlong)token);
        if ((*env)->ExceptionCheck(env))
            (*env)->ExceptionClear(env);
    }
    (*env)->PopLocalFrame(env, NULL);
}

static void android_cancel_all(void* user)
{
    MEL_UNUSED(user);
    JNIEnv* env = mel_platform_android_env();
    if (env == NULL || (*env)->PushLocalFrame(env, 8) != 0)
        return;
    jobject ctx = android_context(env);
    jclass  cls = ctx != NULL ? helper_class(env) : NULL;
    if (cls != NULL)
    {
        jmethodID m = (*env)->GetStaticMethodID(env, cls, "cancelAll", "(Landroid/content/Context;)V");
        if (m != NULL)
            (*env)->CallStaticVoidMethod(env, cls, m, ctx);
        if ((*env)->ExceptionCheck(env))
            (*env)->ExceptionClear(env);
    }
    (*env)->PopLocalFrame(env, NULL);
}

static void android_shutdown(void* user)
{
    MEL_UNUSED(user);
    if (ax.listening)
        mel_platform_android_permission_unlisten(MEL_NOTIF_PERMISSION_REQUEST_CODE, on_permission_result, NULL);
    memset(&ax, 0, sizeof ax);
}

void mel_notif__register_host_providers(void)
{
    static const Mel_Notif_Provider_Desc desc = {
        .name = "android-notification-manager",
        .supported = android_supported,
        .caps = android_caps,
        .authorization = android_authorization,
        .authorize = android_authorize,
        .channel_register = android_channel_register,
        .post = android_post,
        .update = android_post,
        .cancel = android_cancel,
        .cancel_all = android_cancel_all,
        .shutdown = android_shutdown,
    };
    mel_notif_provider_register(&desc);
}
