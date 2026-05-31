#include <power/power.h>

#include "../power_str.h"

#include <platform/android/jni.h>

static jobject mel_android_app_context(JNIEnv* env)
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

static jobject mel_android_system_service(JNIEnv* env, jobject ctx, const char* name)
{
    jclass ctx_class = (*env)->FindClass(env, "android/content/Context");
    if (!ctx_class)
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    jmethodID get = (*env)->GetMethodID(env, ctx_class, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    if (!get)
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    jstring jname = (*env)->NewStringUTF(env, name);
    jobject svc = (*env)->CallObjectMethod(env, ctx, get, jname);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    return svc;
}

static jobject mel_android_battery_intent(JNIEnv* env, jobject ctx)
{
    jclass if_class = (*env)->FindClass(env, "android/content/IntentFilter");
    jclass ctx_class = (*env)->FindClass(env, "android/content/Context");
    if (!if_class || !ctx_class)
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    jmethodID if_ctor = (*env)->GetMethodID(env, if_class, "<init>", "(Ljava/lang/String;)V");
    jmethodID reg = (*env)->GetMethodID(env, ctx_class, "registerReceiver", "(Landroid/content/BroadcastReceiver;Landroid/content/IntentFilter;)Landroid/content/Intent;");
    if (!if_ctor || !reg)
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    jstring action = (*env)->NewStringUTF(env, "android.intent.action.BATTERY_CHANGED");
    jobject filter = (*env)->NewObject(env, if_class, if_ctor, action);
    jobject intent = (*env)->CallObjectMethod(env, ctx, reg, NULL, filter);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    return intent;
}

static jint mel_android_intent_int(JNIEnv* env, jobject intent, const char* key, jint fallback)
{
    jclass    ic = (*env)->GetObjectClass(env, intent);
    jmethodID get = (*env)->GetMethodID(env, ic, "getIntExtra", "(Ljava/lang/String;I)I");
    if (!get)
    {
        (*env)->ExceptionClear(env);
        return fallback;
    }
    jstring k = (*env)->NewStringUTF(env, key);
    jint    v = (*env)->CallIntMethod(env, intent, get, k, fallback);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return fallback;
    }
    return v;
}

Mel_Power_Source mel_power_source_current(void)
{
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return MEL_POWER_SOURCE_UNKNOWN;
    if ((*env)->PushLocalFrame(env, 16) != 0)
        return MEL_POWER_SOURCE_UNKNOWN;

    Mel_Power_Source out = MEL_POWER_SOURCE_UNKNOWN;
    jobject          ctx = mel_android_app_context(env);
    jobject          intent = ctx ? mel_android_battery_intent(env, ctx) : NULL;
    if (intent)
    {
        jint plugged = mel_android_intent_int(env, intent, "plugged", -1);
        if (plugged > 0)
            out = MEL_POWER_SOURCE_AC;
        else if (plugged == 0)
            out = MEL_POWER_SOURCE_BATTERY;
    }
    (*env)->PopLocalFrame(env, NULL);
    return out;
}

Mel_Power_Profile mel_power_profile_current(void)
{
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return (Mel_Power_Profile){ 0 };
    if ((*env)->PushLocalFrame(env, 16) != 0)
        return (Mel_Power_Profile){ 0 };

    Mel_Power_Profile out = { 0 };
    jobject           ctx = mel_android_app_context(env);
    jobject           pm = ctx ? mel_android_system_service(env, ctx, "power") : NULL;
    if (pm)
    {
        jclass    pm_class = (*env)->GetObjectClass(env, pm);
        jmethodID m = (*env)->GetMethodID(env, pm_class, "isPowerSaveMode", "()Z");
        if (m)
        {
            jboolean on = (*env)->CallBooleanMethod(env, pm, m);
            if (!(*env)->ExceptionCheck(env))
            {
                out.present = true;
                out.bias = on ? -1.0f : 0.0f;
            }
            else
                (*env)->ExceptionClear(env);
        }
        else
            (*env)->ExceptionClear(env);
    }
    (*env)->PopLocalFrame(env, NULL);
    return out;
}

bool mel_power_profile_name(char* buf, usize cap)
{
    Mel_Power_Profile p = mel_power_profile_current();
    if (!p.present)
        return false;
    return mel_power_name_copy(buf, cap, p.bias < 0.0f ? "Power saving" : "Normal");
}

Mel_Power_Battery mel_power_battery_current(void)
{
    Mel_Power_Battery out = { false, false, 0.0f, -1.0f, -1.0f };

    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return out;
    if ((*env)->PushLocalFrame(env, 16) != 0)
        return out;

    jobject ctx = mel_android_app_context(env);
    jobject intent = ctx ? mel_android_battery_intent(env, ctx) : NULL;
    if (intent)
    {
        jint level = mel_android_intent_int(env, intent, "level", -1);
        jint scale = mel_android_intent_int(env, intent, "scale", -1);
        jint status = mel_android_intent_int(env, intent, "status", 1);
        if (level >= 0 && scale > 0)
        {
            out.present = true;
            out.level = (f32)level / (f32)scale;
        }
        out.charging = (status == 2);
    }
    (*env)->PopLocalFrame(env, NULL);
    return out;
}

Mel_Power_Caps mel_power_caps(void)
{
    JNIEnv* env = mel_platform_android_env();
    bool    framework = (env != NULL);
    return (Mel_Power_Caps){
        .power_source_present = framework,
        .profile_present = framework,
        .battery_present = framework,
    };
}
