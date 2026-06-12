#include <core/platform.h>

#if !MEL_PLATFORM_ANDROID
#error "android-only translation unit"
#endif

#include "audioout_android_internal.h"

#include <platform/android/jni.h>
#include <allocator/allocator.h>
#include <log/log.h>

#include <string.h>

#define MEL_AUDIOOUT_ANDROID_GET_DEVICES_OUTPUTS 2

static jobject aout_jni_context(JNIEnv* env)
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

static jclass aout_jni_helper_class(JNIEnv* env)
{
    jclass cls = mel_platform_android_find_class(env, "orgwall/melody/audioout/MelodyAudioOut");
    if (!cls)
    {
        (*env)->ExceptionClear(env);
        mel_log_error("audioout", "android: MelodyAudioOut Java helper not found");
    }
    return cls;
}

bool mel_audioout_android__hotplug_start(void)
{
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return false;
    if ((*env)->PushLocalFrame(env, 4) < 0)
        return false;

    jclass cls = aout_jni_helper_class(env);
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
        mel_log_error("audioout", "android: MelodyAudioOut.startDeviceListening missing; hotplug disabled");
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
        mel_log_error("audioout", "android: device callback registration failed; hotplug disabled");
    return ok == JNI_TRUE;
}

void mel_audioout_android__hotplug_stop(void)
{
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return;
    if ((*env)->PushLocalFrame(env, 4) < 0)
        return;

    jclass cls = aout_jni_helper_class(env);
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

static str8 aout_jni_string_dup(JNIEnv* env, jstring js, const Mel_Alloc* alloc)
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

static void aout_jni_ints_collect(JNIEnv* env, jintArray arr, Mel_AudioOut_Rates* out)
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

void mel_audioout_android__jni_devices_free(Mel_AudioOut_Android_Devices* devices, const Mel_Alloc* alloc)
{
    for (usize i = 0; i < devices->count; i++)
    {
        Mel_AudioOut_Android_Device* d = &devices->items[i];
        if (d->name.data)
            mel_dealloc(alloc, d->name.data);
        if (d->address.data)
            mel_dealloc(alloc, d->address.data);
        mel_array_free(&d->channel_counts);
        mel_array_free(&d->sample_rates);
    }
    mel_array_free(devices);
}

bool mel_audioout_android__jni_enumerate(const Mel_Alloc* alloc, Mel_AudioOut_Android_Devices* out)
{
    JNIEnv* env = mel_platform_android_env();
    if (!env)
    {
        mel_log_error("audioout", "android: no JNI environment; cannot enumerate outputs");
        return false;
    }
    if ((*env)->PushLocalFrame(env, 32) < 0)
        return false;

    jobject ctx = aout_jni_context(env);
    if (!ctx)
    {
        mel_log_error("audioout", "android: no application context; cannot enumerate outputs");
        goto fail;
    }

    jclass    ctx_cls = (*env)->GetObjectClass(env, ctx);
    jmethodID get_service = (*env)->GetMethodID(env, ctx_cls, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    if (!get_service)
    {
        (*env)->ExceptionClear(env);
        mel_log_error("audioout", "android: Context.getSystemService missing");
        goto fail;
    }
    jstring svc_name = (*env)->NewStringUTF(env, "audio");
    jobject manager = (*env)->CallObjectMethod(env, ctx, get_service, svc_name);
    if ((*env)->ExceptionCheck(env) || !manager)
    {
        (*env)->ExceptionClear(env);
        mel_log_error("audioout", "android: AudioManager unavailable");
        goto fail;
    }

    jclass    am_cls = (*env)->GetObjectClass(env, manager);
    jmethodID get_devices = (*env)->GetMethodID(env, am_cls, "getDevices", "(I)[Landroid/media/AudioDeviceInfo;");
    if (!get_devices)
    {
        (*env)->ExceptionClear(env);
        mel_log_error("audioout", "android: AudioManager.getDevices missing");
        goto fail;
    }
    jobjectArray arr = (jobjectArray)(*env)->CallObjectMethod(env, manager, get_devices, MEL_AUDIOOUT_ANDROID_GET_DEVICES_OUTPUTS);
    if ((*env)->ExceptionCheck(env) || !arr)
    {
        (*env)->ExceptionClear(env);
        mel_log_error("audioout", "android: AudioManager.getDevices failed");
        goto fail;
    }

    jclass dev_cls = mel_platform_android_find_class(env, "android/media/AudioDeviceInfo");
    if (!dev_cls)
    {
        (*env)->ExceptionClear(env);
        mel_log_error("audioout", "android: AudioDeviceInfo class not found");
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
        mel_log_error("audioout", "android: AudioDeviceInfo accessors missing");
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

        Mel_AudioOut_Android_Device d;
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
                    d.name = aout_jni_string_dup(env, jname, alloc);
            }
        }

        if (m_addr)
        {
            jstring jaddr = (jstring)(*env)->CallObjectMethod(env, dev, m_addr);
            if (!(*env)->ExceptionCheck(env))
                d.address = aout_jni_string_dup(env, jaddr, alloc);
        }

        jintArray chans = (jintArray)(*env)->CallObjectMethod(env, dev, m_chans);
        if (!(*env)->ExceptionCheck(env))
            aout_jni_ints_collect(env, chans, &d.channel_counts);
        jintArray rates = (jintArray)(*env)->CallObjectMethod(env, dev, m_rates);
        if (!(*env)->ExceptionCheck(env))
            aout_jni_ints_collect(env, rates, &d.sample_rates);
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

JNIEXPORT void JNICALL Java_orgwall_melody_audioout_MelodyAudioOut_nativeDevicesChanged(JNIEnv* env, jclass cls)
{
    MEL_UNUSED(env);
    MEL_UNUSED(cls);
    mel_audioout_android__on_devices_changed();
}
