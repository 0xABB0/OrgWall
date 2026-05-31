#include <power/power.h>

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

Mel_Power_Source mel_power_source_current(void)
{
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return MEL_POWER_SOURCE_UNKNOWN;
    if ((*env)->PushLocalFrame(env, 16) != 0)
        return MEL_POWER_SOURCE_UNKNOWN;

    Mel_Power_Source out = MEL_POWER_SOURCE_UNKNOWN;
    jobject          ctx = mel_android_app_context(env);
    if (ctx)
    {
        jclass if_class = (*env)->FindClass(env, "android/content/IntentFilter");
        jclass ctx_class = (*env)->FindClass(env, "android/content/Context");
        if (if_class && ctx_class)
        {
            jmethodID if_ctor = (*env)->GetMethodID(env, if_class, "<init>", "(Ljava/lang/String;)V");
            jmethodID reg = (*env)->GetMethodID(env, ctx_class, "registerReceiver", "(Landroid/content/BroadcastReceiver;Landroid/content/IntentFilter;)Landroid/content/Intent;");
            if (if_ctor && reg)
            {
                jstring action = (*env)->NewStringUTF(env, "android.intent.action.BATTERY_CHANGED");
                jobject filter = (*env)->NewObject(env, if_class, if_ctor, action);
                jobject intent = (*env)->CallObjectMethod(env, ctx, reg, NULL, filter);
                if (!(*env)->ExceptionCheck(env) && intent)
                {
                    jclass    intent_class = (*env)->GetObjectClass(env, intent);
                    jmethodID get_int = (*env)->GetMethodID(env, intent_class, "getIntExtra", "(Ljava/lang/String;I)I");
                    if (get_int)
                    {
                        jstring key = (*env)->NewStringUTF(env, "plugged");
                        jint    plugged = (*env)->CallIntMethod(env, intent, get_int, key, -1);
                        if (!(*env)->ExceptionCheck(env))
                        {
                            if (plugged > 0)
                                out = MEL_POWER_SOURCE_AC;
                            else if (plugged == 0)
                                out = MEL_POWER_SOURCE_BATTERY;
                        }
                    }
                }
                (*env)->ExceptionClear(env);
            }
            else
            {
                (*env)->ExceptionClear(env);
            }
        }
        else
        {
            (*env)->ExceptionClear(env);
        }
    }
    (*env)->PopLocalFrame(env, NULL);
    return out;
}

Mel_Power_Low_Power_Mode mel_power_low_power_current(void)
{
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return MEL_POWER_LOW_POWER_UNKNOWN;
    if ((*env)->PushLocalFrame(env, 16) != 0)
        return MEL_POWER_LOW_POWER_UNKNOWN;

    Mel_Power_Low_Power_Mode out = MEL_POWER_LOW_POWER_UNKNOWN;
    jobject                  ctx = mel_android_app_context(env);
    jobject                  pm = ctx ? mel_android_system_service(env, ctx, "power") : NULL;
    if (pm)
    {
        jclass    pm_class = (*env)->GetObjectClass(env, pm);
        jmethodID m = (*env)->GetMethodID(env, pm_class, "isPowerSaveMode", "()Z");
        if (m)
        {
            jboolean on = (*env)->CallBooleanMethod(env, pm, m);
            if (!(*env)->ExceptionCheck(env))
                out = on ? MEL_POWER_LOW_POWER_ON : MEL_POWER_LOW_POWER_OFF;
            else
                (*env)->ExceptionClear(env);
        }
        else
        {
            (*env)->ExceptionClear(env);
        }
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
        .low_power_present = framework,
    };
}
