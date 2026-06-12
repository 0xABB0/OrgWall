#include <core/platform.h>

#if !MEL_PLATFORM_ANDROID
#error "android-only translation unit"
#endif

#include "policy_android_internal.h"

#include <platform/android/jni.h>
#include <log/log.h>

static jclass apolicy_jni_helper_class(JNIEnv* env)
{
    jclass cls = mel_platform_android_find_class(env, "orgwall/melody/audiopolicy/MelodyAudioPolicy");
    if (!cls)
    {
        (*env)->ExceptionClear(env);
        mel_log_error("audiopolicy", "android: MelodyAudioPolicy Java helper not found");
    }
    return cls;
}

static jmethodID apolicy_jni_static_method(JNIEnv* env, jclass cls, const char* name, const char* sig)
{
    jmethodID m = (*env)->GetStaticMethodID(env, cls, name, sig);
    if (!m)
    {
        (*env)->ExceptionClear(env);
        mel_log_error("audiopolicy", "android: MelodyAudioPolicy.%s missing", name);
    }
    return m;
}

bool mel_audiopolicy_android__jni_set_mode(i32 mode)
{
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return false;
    if ((*env)->PushLocalFrame(env, 4) < 0)
        return false;

    jboolean ok = JNI_FALSE;
    jclass   cls = apolicy_jni_helper_class(env);
    if (cls)
    {
        jmethodID m = apolicy_jni_static_method(env, cls, "setMode", "(I)Z");
        if (m)
        {
            ok = (*env)->CallStaticBooleanMethod(env, cls, m, (jint)mode);
            if ((*env)->ExceptionCheck(env))
            {
                (*env)->ExceptionClear(env);
                ok = JNI_FALSE;
            }
        }
    }
    (*env)->PopLocalFrame(env, NULL);
    return ok == JNI_TRUE;
}

i32 mel_audiopolicy_android__jni_request_focus(i32 gain_type, i32 usage, i32 content_type, bool pause_when_ducked)
{
    JNIEnv* env = mel_platform_android_env();
    if (!env)
    {
        mel_log_error("audiopolicy", "android: no JNI environment; cannot request focus");
        return MEL_AUDIOPOLICY_ANDROID_REQUEST_FAILED;
    }
    if ((*env)->PushLocalFrame(env, 4) < 0)
        return MEL_AUDIOPOLICY_ANDROID_REQUEST_FAILED;

    jint   res = MEL_AUDIOPOLICY_ANDROID_REQUEST_FAILED;
    jclass cls = apolicy_jni_helper_class(env);
    if (cls)
    {
        jmethodID m = apolicy_jni_static_method(env, cls, "requestFocus", "(IIIZ)I");
        if (m)
        {
            res = (*env)->CallStaticIntMethod(env, cls, m, (jint)gain_type, (jint)usage, (jint)content_type, pause_when_ducked ? JNI_TRUE : JNI_FALSE);
            if ((*env)->ExceptionCheck(env))
            {
                (*env)->ExceptionClear(env);
                res = MEL_AUDIOPOLICY_ANDROID_REQUEST_FAILED;
            }
        }
    }
    (*env)->PopLocalFrame(env, NULL);
    return (i32)res;
}

void mel_audiopolicy_android__jni_abandon_focus(void)
{
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return;
    if ((*env)->PushLocalFrame(env, 4) < 0)
        return;

    jclass cls = apolicy_jni_helper_class(env);
    if (cls)
    {
        jmethodID m = apolicy_jni_static_method(env, cls, "abandonFocus", "()V");
        if (m)
        {
            (*env)->CallStaticVoidMethod(env, cls, m);
            if ((*env)->ExceptionCheck(env))
                (*env)->ExceptionClear(env);
        }
    }
    (*env)->PopLocalFrame(env, NULL);
}

bool mel_audiopolicy_android__jni_speaker_communication_device(void)
{
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return false;
    if ((*env)->PushLocalFrame(env, 4) < 0)
        return false;

    jboolean ok = JNI_FALSE;
    jclass   cls = apolicy_jni_helper_class(env);
    if (cls)
    {
        jmethodID m = apolicy_jni_static_method(env, cls, "setSpeakerCommunicationDevice", "()Z");
        if (m)
        {
            ok = (*env)->CallStaticBooleanMethod(env, cls, m);
            if ((*env)->ExceptionCheck(env))
            {
                (*env)->ExceptionClear(env);
                ok = JNI_FALSE;
            }
        }
    }
    (*env)->PopLocalFrame(env, NULL);
    return ok == JNI_TRUE;
}

void mel_audiopolicy_android__jni_clear_communication_device(void)
{
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return;
    if ((*env)->PushLocalFrame(env, 4) < 0)
        return;

    jclass cls = apolicy_jni_helper_class(env);
    if (cls)
    {
        jmethodID m = apolicy_jni_static_method(env, cls, "clearCommunicationDevice", "()V");
        if (m)
        {
            (*env)->CallStaticVoidMethod(env, cls, m);
            if ((*env)->ExceptionCheck(env))
                (*env)->ExceptionClear(env);
        }
    }
    (*env)->PopLocalFrame(env, NULL);
}

JNIEXPORT void JNICALL Java_orgwall_melody_audiopolicy_MelodyAudioPolicy_nativeFocusChange(JNIEnv* env, jclass cls, jint change)
{
    MEL_UNUSED(env);
    MEL_UNUSED(cls);
    mel_audiopolicy_android__on_focus_change((i32)change);
}
