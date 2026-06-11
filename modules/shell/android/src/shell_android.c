#include <shell/backend.h>

#include <platform/android/jni.h>
#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <string/str8.h>
#include <log/log.h>

#include <string.h>

bool mel_shell__plat_available(void) { return true; }

static jstring jstr_from_str8(JNIEnv* env, str8 s)
{
    const Mel_Alloc* a = mel_alloc_heap();
    char*            c = (char*)mel_alloc(a, (usize)s.len + 1);
    if (!c)
        return (*env)->NewStringUTF(env, "");
    if (s.len)
        memcpy(c, s.data, (usize)s.len);
    c[s.len] = 0;
    jstring j = (*env)->NewStringUTF(env, c);
    mel_dealloc(a, c);
    return j;
}

static jobject android_context(JNIEnv* env)
{
    jclass at = (*env)->FindClass(env, "android/app/ActivityThread");
    if (!at)
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    jmethodID cur = (*env)->GetStaticMethodID(env, at, "currentApplication", "()Landroid/app/Application;");
    if (!cur)
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

static jobject parse_uri(JNIEnv* env, str8 target)
{
    jclass uri_cls = (*env)->FindClass(env, "android/net/Uri");
    if (!uri_cls)
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    jmethodID parse = (*env)->GetStaticMethodID(env, uri_cls, "parse", "(Ljava/lang/String;)Landroid/net/Uri;");
    jmethodID from_file = (*env)->GetStaticMethodID(env, uri_cls, "fromFile", "(Ljava/io/File;)Landroid/net/Uri;");
    if (str8_starts_with(target, S8("/")))
    {
        jclass    file_cls = (*env)->FindClass(env, "java/io/File");
        jmethodID file_ctor = file_cls ? (*env)->GetMethodID(env, file_cls, "<init>", "(Ljava/lang/String;)V") : NULL;
        if (file_cls && file_ctor && from_file)
        {
            jobject f = (*env)->NewObject(env, file_cls, file_ctor, jstr_from_str8(env, target));
            return f ? (*env)->CallStaticObjectMethod(env, uri_cls, from_file, f) : NULL;
        }
    }
    return parse ? (*env)->CallStaticObjectMethod(env, uri_cls, parse, jstr_from_str8(env, target)) : NULL;
}

static Mel_Shell_Status start_view(JNIEnv* env, jobject ctx, jobject uri, const char* mime)
{
    jclass intent_cls = (*env)->FindClass(env, "android/content/Intent");
    if (!intent_cls)
    {
        (*env)->ExceptionClear(env);
        return MEL_SHELL_ERROR | MEL_SHELL_RESULT_NO_HANDLER;
    }
    jfieldID  action_view_f = (*env)->GetStaticFieldID(env, intent_cls, "ACTION_VIEW", "Ljava/lang/String;");
    jstring   action_view = (*env)->GetStaticObjectField(env, intent_cls, action_view_f);
    jmethodID ctor = (*env)->GetMethodID(env, intent_cls, "<init>", "(Ljava/lang/String;Landroid/net/Uri;)V");
    jobject   intent = ctor ? (*env)->NewObject(env, intent_cls, ctor, action_view, uri) : NULL;
    if (!intent)
    {
        (*env)->ExceptionClear(env);
        return MEL_SHELL_ERROR | MEL_SHELL_RESULT_NO_HANDLER;
    }

    jfieldID  flag_f = (*env)->GetStaticFieldID(env, intent_cls, "FLAG_ACTIVITY_NEW_TASK", "I");
    jint      flag = (*env)->GetStaticIntField(env, intent_cls, flag_f);
    jmethodID add_flags = (*env)->GetMethodID(env, intent_cls, "addFlags", "(I)Landroid/content/Intent;");
    if (add_flags)
        (*env)->CallObjectMethod(env, intent, add_flags, flag);

    if (mime)
    {
        jmethodID set_data_type = (*env)->GetMethodID(env, intent_cls, "setDataAndType", "(Landroid/net/Uri;Ljava/lang/String;)Landroid/content/Intent;");
        if (set_data_type)
            (*env)->CallObjectMethod(env, intent, set_data_type, uri, (*env)->NewStringUTF(env, mime));
    }

    jclass    ctx_cls = (*env)->GetObjectClass(env, ctx);
    jmethodID start = (*env)->GetMethodID(env, ctx_cls, "startActivity", "(Landroid/content/Intent;)V");
    if (!start)
    {
        (*env)->ExceptionClear(env);
        return MEL_SHELL_ERROR | MEL_SHELL_RESULT_NO_HANDLER;
    }
    (*env)->CallVoidMethod(env, ctx, start, intent);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return MEL_SHELL_ERROR | MEL_SHELL_RESULT_NO_HANDLER;
    }
    return MEL_SHELL_OK;
}

void mel_shell__plat_open_url(Mel_Shell_Job* job)
{
    JNIEnv* env = mel_platform_android_env();
    if (!env || (*env)->PushLocalFrame(env, 32) != 0)
    {
        mel_shell_job_resolve(job, MEL_SHELL_ERROR | MEL_SHELL_RESULT_NO_BACKEND);
        return;
    }
    jobject          ctx = android_context(env);
    jobject          uri = ctx ? parse_uri(env, mel_shell_job_target(job)) : NULL;
    Mel_Shell_Status s = (ctx && uri) ? start_view(env, ctx, uri, NULL) : (MEL_SHELL_ERROR | MEL_SHELL_RESULT_BAD_TARGET);
    (*env)->PopLocalFrame(env, NULL);
    mel_shell_job_resolve(job, s);
}

void mel_shell__plat_reveal_path(Mel_Shell_Job* job)
{
    JNIEnv* env = mel_platform_android_env();
    if (!env || (*env)->PushLocalFrame(env, 32) != 0)
    {
        mel_shell_job_resolve(job, MEL_SHELL_ERROR | MEL_SHELL_RESULT_NO_BACKEND);
        return;
    }
    jobject ctx = android_context(env);
    jobject uri = ctx ? parse_uri(env, mel_shell_job_target(job)) : NULL;
    if (!ctx || !uri)
    {
        (*env)->PopLocalFrame(env, NULL);
        mel_shell_job_resolve(job, MEL_SHELL_ERROR | MEL_SHELL_RESULT_BAD_TARGET);
        return;
    }
    Mel_Shell_Status s = start_view(env, ctx, uri, "resource/folder");
    (*env)->PopLocalFrame(env, NULL);
    if (mel_shell_ok(s))
        mel_shell_job_resolve(job, MEL_SHELL_WARNED | MEL_SHELL_WARN_REVEAL_DEGRADED);
    else
        mel_shell_job_resolve(job, s);
}

void* mel_shell__plat_native(void) { return NULL; }
