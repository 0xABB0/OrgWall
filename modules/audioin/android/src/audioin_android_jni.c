#include <core/platform.h>

#if !MEL_PLATFORM_ANDROID
#error "android-only translation unit"
#endif

#include "audioin_android_internal.h"

#include <platform/android/jni.h>
#include <allocator/allocator.h>
#include <log/log.h>

#define MEL_AUDIOIN_ANDROID_PERMISSION         "android.permission.RECORD_AUDIO"
#define MEL_AUDIOIN_ANDROID_PERMISSION_GRANTED 0
#define MEL_AUDIOIN_ANDROID_GET_DEVICES_INPUTS 1

static jobject ain_jni_context(JNIEnv* env)
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

bool mel_audioin_android__permission_granted(void)
{
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return false;
    if ((*env)->PushLocalFrame(env, 8) < 0)
        return false;

    jobject ctx = ain_jni_context(env);
    if (!ctx)
    {
        (*env)->PopLocalFrame(env, NULL);
        return false;
    }

    jclass    ctx_cls = (*env)->GetObjectClass(env, ctx);
    jmethodID check = (*env)->GetMethodID(env, ctx_cls, "checkSelfPermission", "(Ljava/lang/String;)I");
    if (!check)
    {
        (*env)->ExceptionClear(env);
        (*env)->PopLocalFrame(env, NULL);
        return false;
    }
    jstring perm = (*env)->NewStringUTF(env, MEL_AUDIOIN_ANDROID_PERMISSION);
    jint    res = (*env)->CallIntMethod(env, ctx, check, perm);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        (*env)->PopLocalFrame(env, NULL);
        return false;
    }
    (*env)->PopLocalFrame(env, NULL);
    return res == MEL_AUDIOIN_ANDROID_PERMISSION_GRANTED;
}

static jclass ain_jni_helper_class(JNIEnv* env)
{
    jclass cls = mel_platform_android_find_class(env, "orgwall/melody/audioin/MelodyAudioIn");
    if (!cls)
    {
        (*env)->ExceptionClear(env);
        mel_log_error("audioin", "android: MelodyAudioIn Java helper not found");
    }
    return cls;
}

bool mel_audioin_android__request_permission(void)
{
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return false;
    if ((*env)->PushLocalFrame(env, 4) < 0)
        return false;

    jclass cls = ain_jni_helper_class(env);
    if (!cls)
    {
        (*env)->PopLocalFrame(env, NULL);
        return false;
    }
    jmethodID req = (*env)->GetStaticMethodID(env, cls, "requestPermission", "()Z");
    if (!req)
    {
        (*env)->ExceptionClear(env);
        (*env)->PopLocalFrame(env, NULL);
        mel_log_error("audioin", "android: MelodyAudioIn.requestPermission missing");
        return false;
    }
    jboolean ok = (*env)->CallStaticBooleanMethod(env, cls, req);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        (*env)->PopLocalFrame(env, NULL);
        return false;
    }
    (*env)->PopLocalFrame(env, NULL);
    return ok == JNI_TRUE;
}

bool mel_audioin_android__hotplug_start(void)
{
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return false;
    if ((*env)->PushLocalFrame(env, 4) < 0)
        return false;

    jclass cls = ain_jni_helper_class(env);
    if (!cls)
    {
        (*env)->PopLocalFrame(env, NULL);
        return false;
    }
    jmethodID start = (*env)->GetStaticMethodID(env, cls, "startDeviceListening", "()Z");
    if (!start)
    {
        (*env)->ExceptionClear(env);
        (*env)->PopLocalFrame(env, NULL);
        mel_log_error("audioin", "android: MelodyAudioIn.startDeviceListening missing; hotplug disabled");
        return false;
    }
    jboolean ok = (*env)->CallStaticBooleanMethod(env, cls, start);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        ok = JNI_FALSE;
    }
    (*env)->PopLocalFrame(env, NULL);
    if (ok != JNI_TRUE)
        mel_log_error("audioin", "android: device callback registration failed; hotplug disabled");
    return ok == JNI_TRUE;
}

void mel_audioin_android__hotplug_stop(void)
{
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return;
    if ((*env)->PushLocalFrame(env, 4) < 0)
        return;

    jclass cls = ain_jni_helper_class(env);
    if (cls)
    {
        jmethodID stop = (*env)->GetStaticMethodID(env, cls, "stopDeviceListening", "()V");
        if (stop)
            (*env)->CallStaticVoidMethod(env, cls, stop);
        if ((*env)->ExceptionCheck(env))
            (*env)->ExceptionClear(env);
    }
    (*env)->PopLocalFrame(env, NULL);
}

static str8 ain_jni_string_dup(JNIEnv* env, jstring js, const Mel_Alloc* alloc)
{
    if (!js)
        return STR8_EMPTY;
    const char* utf = (*env)->GetStringUTFChars(env, js, NULL);
    if (!utf)
        return STR8_EMPTY;
    str8 s = str8_dup(str8_from_cstr(utf), alloc);
    (*env)->ReleaseStringUTFChars(env, js, utf);
    return s;
}

static void ain_jni_ints_collect(JNIEnv* env, jintArray arr, Mel_AudioIn_Rates* out)
{
    if (!arr)
        return;
    jsize n = (*env)->GetArrayLength(env, arr);
    if (n <= 0)
        return;
    jint* vals = (*env)->GetIntArrayElements(env, arr, NULL);
    if (!vals)
        return;
    for (jsize i = 0; i < n; i++)
        if (vals[i] > 0)
            mel_array_push(out, (u32)vals[i]);
    (*env)->ReleaseIntArrayElements(env, arr, vals, JNI_ABORT);
}

void mel_audioin_android__jni_devices_free(Mel_AudioIn_Android_Devices* devices, const Mel_Alloc* alloc)
{
    for (usize i = 0; i < devices->count; i++)
    {
        Mel_AudioIn_Android_Device* d = &devices->items[i];
        if (d->name.data)
            mel_dealloc(alloc, d->name.data);
        if (d->address.data)
            mel_dealloc(alloc, d->address.data);
        mel_array_free(&d->channel_counts);
        mel_array_free(&d->sample_rates);
    }
    mel_array_free(devices);
}

bool mel_audioin_android__jni_enumerate(const Mel_Alloc* alloc, Mel_AudioIn_Android_Devices* out)
{
    JNIEnv* env = mel_platform_android_env();
    if (!env)
    {
        mel_log_error("audioin", "android: no JNI environment; cannot enumerate inputs");
        return false;
    }
    if ((*env)->PushLocalFrame(env, 32) < 0)
        return false;

    jobject ctx = ain_jni_context(env);
    if (!ctx)
    {
        mel_log_error("audioin", "android: no application context; cannot enumerate inputs");
        goto fail;
    }

    jclass    ctx_cls = (*env)->GetObjectClass(env, ctx);
    jmethodID get_service = (*env)->GetMethodID(env, ctx_cls, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    if (!get_service)
    {
        (*env)->ExceptionClear(env);
        mel_log_error("audioin", "android: Context.getSystemService missing");
        goto fail;
    }
    jstring svc_name = (*env)->NewStringUTF(env, "audio");
    jobject manager = (*env)->CallObjectMethod(env, ctx, get_service, svc_name);
    if ((*env)->ExceptionCheck(env) || !manager)
    {
        (*env)->ExceptionClear(env);
        mel_log_error("audioin", "android: AudioManager unavailable");
        goto fail;
    }

    jclass    am_cls = (*env)->GetObjectClass(env, manager);
    jmethodID get_devices = (*env)->GetMethodID(env, am_cls, "getDevices", "(I)[Landroid/media/AudioDeviceInfo;");
    if (!get_devices)
    {
        (*env)->ExceptionClear(env);
        mel_log_error("audioin", "android: AudioManager.getDevices missing");
        goto fail;
    }
    jobjectArray arr = (jobjectArray)(*env)->CallObjectMethod(env, manager, get_devices, MEL_AUDIOIN_ANDROID_GET_DEVICES_INPUTS);
    if ((*env)->ExceptionCheck(env) || !arr)
    {
        (*env)->ExceptionClear(env);
        mel_log_error("audioin", "android: AudioManager.getDevices failed");
        goto fail;
    }

    jclass dev_cls = mel_platform_android_find_class(env, "android/media/AudioDeviceInfo");
    if (!dev_cls)
    {
        (*env)->ExceptionClear(env);
        mel_log_error("audioin", "android: AudioDeviceInfo class not found");
        goto fail;
    }
    jmethodID m_id = (*env)->GetMethodID(env, dev_cls, "getId", "()I");
    jmethodID m_type = (*env)->GetMethodID(env, dev_cls, "getType", "()I");
    jmethodID m_name = (*env)->GetMethodID(env, dev_cls, "getProductName", "()Ljava/lang/CharSequence;");
    jmethodID m_chans = (*env)->GetMethodID(env, dev_cls, "getChannelCounts", "()[I");
    jmethodID m_rates = (*env)->GetMethodID(env, dev_cls, "getSampleRates", "()[I");
    if (!m_id || !m_type || !m_name || !m_chans || !m_rates)
    {
        (*env)->ExceptionClear(env);
        mel_log_error("audioin", "android: AudioDeviceInfo accessors missing");
        goto fail;
    }
    jmethodID m_addr = (*env)->GetMethodID(env, dev_cls, "getAddress", "()Ljava/lang/String;");
    if (!m_addr)
        (*env)->ExceptionClear(env);

    jsize n = (*env)->GetArrayLength(env, arr);
    for (jsize i = 0; i < n; i++)
    {
        if ((*env)->PushLocalFrame(env, 16) < 0)
            break;
        jobject dev = (*env)->GetObjectArrayElement(env, arr, i);
        if (!dev)
        {
            (*env)->PopLocalFrame(env, NULL);
            continue;
        }

        Mel_AudioIn_Android_Device d;
        memset(&d, 0, sizeof d);
        mel_array_init(&d.channel_counts, alloc);
        mel_array_init(&d.sample_rates, alloc);
        d.id = (i32)(*env)->CallIntMethod(env, dev, m_id);
        d.type = (i32)(*env)->CallIntMethod(env, dev, m_type);

        jobject name_cs = (*env)->CallObjectMethod(env, dev, m_name);
        if (!(*env)->ExceptionCheck(env) && name_cs)
        {
            jclass    cs_cls = (*env)->GetObjectClass(env, name_cs);
            jmethodID m_str = (*env)->GetMethodID(env, cs_cls, "toString", "()Ljava/lang/String;");
            if (m_str)
            {
                jstring jname = (jstring)(*env)->CallObjectMethod(env, name_cs, m_str);
                if (!(*env)->ExceptionCheck(env))
                    d.name = ain_jni_string_dup(env, jname, alloc);
            }
        }

        if (m_addr)
        {
            jstring jaddr = (jstring)(*env)->CallObjectMethod(env, dev, m_addr);
            if (!(*env)->ExceptionCheck(env))
                d.address = ain_jni_string_dup(env, jaddr, alloc);
        }

        jintArray chans = (jintArray)(*env)->CallObjectMethod(env, dev, m_chans);
        if (!(*env)->ExceptionCheck(env))
            ain_jni_ints_collect(env, chans, &d.channel_counts);
        jintArray rates = (jintArray)(*env)->CallObjectMethod(env, dev, m_rates);
        if (!(*env)->ExceptionCheck(env))
            ain_jni_ints_collect(env, rates, &d.sample_rates);
        if ((*env)->ExceptionCheck(env))
            (*env)->ExceptionClear(env);

        mel_array_push(out, d);
        (*env)->PopLocalFrame(env, NULL);
    }

    (*env)->PopLocalFrame(env, NULL);
    return true;

fail:
    (*env)->PopLocalFrame(env, NULL);
    return false;
}

JNIEXPORT void JNICALL Java_orgwall_melody_audioin_MelodyAudioIn_nativeDevicesChanged(JNIEnv* env, jclass cls)
{
    MEL_UNUSED(env);
    MEL_UNUSED(cls);
    mel_audioin_android__on_devices_changed();
}
