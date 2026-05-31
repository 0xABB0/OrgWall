#include <thermal/thermal.h>

#include <platform/android/jni.h>

#include "../thermal_sysfs.h"

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

Mel_Thermal_Pressure mel_thermal_current(void)
{
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return MEL_THERMAL_UNKNOWN;
    if ((*env)->PushLocalFrame(env, 16) != 0)
        return MEL_THERMAL_UNKNOWN;

    Mel_Thermal_Pressure out = MEL_THERMAL_UNKNOWN;
    jobject              ctx = mel_android_app_context(env);
    jobject              pm = ctx ? mel_android_system_service(env, ctx, "power") : NULL;
    if (pm)
    {
        jclass    pm_class = (*env)->GetObjectClass(env, pm);
        jmethodID m = (*env)->GetMethodID(env, pm_class, "getCurrentThermalStatus", "()I");
        if (m)
        {
            jint s = (*env)->CallIntMethod(env, pm, m);
            if (!(*env)->ExceptionCheck(env))
            {
                switch (s)
                {
                case 0:
                    out = MEL_THERMAL_NOMINAL;
                    break;
                case 1:
                    out = MEL_THERMAL_FAIR;
                    break;
                case 2:
                    out = MEL_THERMAL_SERIOUS;
                    break;
                default:
                    out = MEL_THERMAL_CRITICAL;
                    break;
                }
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

Mel_Thermal_Temperature mel_thermal_temperature(Mel_Thermal_Temp_Domain domain) { return mel_sysfs_temperature(domain); }

Mel_Thermal_Caps mel_thermal_caps(void)
{
    JNIEnv* env = mel_platform_android_env();
    bool    framework = (env != NULL);
    return (Mel_Thermal_Caps){
        .present = framework,
        .temperature = mel_sysfs_temperature(MEL_THERMAL_TEMP_DOMAIN_PRIMARY).fidelity,
    };
}

Mel_Thermal_Sensor_List mel_thermal_sensor_enumerate(const Mel_Alloc* alloc) { return mel_sysfs_sensor_enumerate(alloc); }
