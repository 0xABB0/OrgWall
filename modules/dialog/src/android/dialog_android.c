#include <dialog/backend.h>
#include <window/window.h>

#include <platform/android/jni.h>
#include <log/log.h>

#include <string.h>

#define MELODY_DIALOG_CLASS "orgwall/melody/dialog/MelodyDialog"

static void JNICALL native_on_result(JNIEnv* env, jclass cls, jlong token, jobjectArray paths, jint chosen_filter, jboolean cancelled)
{
    (void)cls;
    Mel_Dialog_Job* job = mel_dialog__job_from_token((u64)token);
    if (!job)
        return;
    if (!cancelled && paths)
    {
        jsize n = (*env)->GetArrayLength(env, paths);
        for (jsize i = 0; i < n; i++)
        {
            jstring s = (jstring)(*env)->GetObjectArrayElement(env, paths, i);
            if (!s)
                continue;
            const char* c = (*env)->GetStringUTFChars(env, s, NULL);
            if (c)
            {
                mel_dialog_job_emit_path(job, c);
                (*env)->ReleaseStringUTFChars(env, s, c);
            }
            (*env)->DeleteLocalRef(env, s);
        }
        mel_dialog_job_set_chosen_filter(job, (u32)chosen_filter);
        mel_dialog_job_add_warning(job, MEL_DIALOG_WARN_FILTER_IGNORED);
        mel_dialog_job_resolve(job, MEL_DIALOG_OK);
    }
    else
    {
        mel_dialog_job_resolve(job, MEL_DIALOG_OK | MEL_DIALOG_CANCELLED);
    }
}

static bool register_natives(JNIEnv* env, jclass cls)
{
    static bool registered = false;
    if (registered)
        return true;
    JNINativeMethod m = { "nativeOnResult", "(J[Ljava/lang/String;IZ)V", (void*)native_on_result };
    if ((*env)->RegisterNatives(env, cls, &m, 1) != 0)
    {
        (*env)->ExceptionClear(env);
        return false;
    }
    registered = true;
    return true;
}

bool mel_dialog__plat_available(void)
{
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return false;
    jclass cls = (*env)->FindClass(env, MELODY_DIALOG_CLASS);
    if (!cls)
    {
        (*env)->ExceptionClear(env);
        return false;
    }
    (*env)->DeleteLocalRef(env, cls);
    return true;
}

static jstring mime_for(JNIEnv* env, const char* ext)
{
    char buf[64];
    if (strstr(ext, "png"))
        snprintf(buf, sizeof buf, "image/png");
    else if (strstr(ext, "jpg") || strstr(ext, "jpeg"))
        snprintf(buf, sizeof buf, "image/jpeg");
    else if (strstr(ext, "txt"))
        snprintf(buf, sizeof buf, "text/plain");
    else if (strstr(ext, "pdf"))
        snprintf(buf, sizeof buf, "application/pdf");
    else if (strstr(ext, "json"))
        snprintf(buf, sizeof buf, "application/json");
    else
        snprintf(buf, sizeof buf, "application/octet-stream");
    return (*env)->NewStringUTF(env, buf);
}

void mel_dialog__plat_run(Mel_Dialog_Job* job)
{
    if (mel_dialog_job_parent(job).index != 0)
        mel_dialog_job_add_warning(job, MEL_DIALOG_WARN_PARENT_IGNORED);

    JNIEnv* env = mel_platform_android_env();
    if (!env)
    {
        mel_dialog_job_resolve(job, MEL_DIALOG_ERROR | MEL_DIALOG_NO_BACKEND);
        return;
    }
    jclass cls = (*env)->FindClass(env, MELODY_DIALOG_CLASS);
    if (!cls || !register_natives(env, cls))
    {
        (*env)->ExceptionClear(env);
        mel_dialog_job_resolve(job, MEL_DIALOG_ERROR | MEL_DIALOG_NO_BACKEND);
        return;
    }

    jmethodID launch = (*env)->GetStaticMethodID(env, cls, "launch", "(JILjava/lang/String;[Ljava/lang/String;Z)V");
    if (!launch)
    {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, cls);
        mel_dialog_job_resolve(job, MEL_DIALOG_ERROR | MEL_DIALOG_NO_BACKEND);
        return;
    }

    u32 request = mel_dialog_job_request(job);

    u32 total = 0;
    u32 fc = mel_dialog_job_filter_count(job);
    for (u32 i = 0; i < fc; i++)
        total += mel_dialog_job_filter_pattern_count(job, i);

    jclass       str_cls = (*env)->FindClass(env, "java/lang/String");
    jobjectArray mimes = (*env)->NewObjectArray(env, (jsize)total, str_cls, NULL);
    u32          idx = 0;
    for (u32 i = 0; i < fc; i++)
    {
        u32 pc = mel_dialog_job_filter_pattern_count(job, i);
        for (u32 p = 0; p < pc; p++)
        {
            const char* pat = mel_dialog_job_filter_pattern(job, i, p);
            jstring     m = mime_for(env, pat ? pat : "");
            (*env)->SetObjectArrayElement(env, mimes, (jsize)idx++, m);
            (*env)->DeleteLocalRef(env, m);
        }
    }

    const char* title = mel_dialog_job_default_name(job) ? mel_dialog_job_default_name(job) : mel_dialog_job_title(job);
    jstring     jtitle = title ? (*env)->NewStringUTF(env, title) : NULL;
    jboolean    multi = (request & MEL_DIALOG_REQUEST_MULTI) ? JNI_TRUE : JNI_FALSE;

    (*env)->CallStaticVoidMethod(env, cls, launch, (jlong)mel_dialog_job_token(job), (jint)request, jtitle, mimes, multi);

    if (jtitle)
        (*env)->DeleteLocalRef(env, jtitle);
    (*env)->DeleteLocalRef(env, mimes);
    (*env)->DeleteLocalRef(env, str_cls);
    (*env)->DeleteLocalRef(env, cls);
}
