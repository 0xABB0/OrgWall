#include <vibration/provider.h>

#include <platform/android/jni.h>
#include <log/log.h>

#define MEL_VIB_ANDROID_STABLE_ID 0x616E64726F696400ULL
#define MEL_VIB_TRANSIENT_FLOOR_S 0.02f

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

static jobject android_vibrator(JNIEnv* env, jobject ctx)
{
    jclass ctx_cls = (*env)->FindClass(env, "android/content/Context");
    if (!ctx_cls)
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    jmethodID get = (*env)->GetMethodID(env, ctx_cls, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    if (!get)
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    jstring name = (*env)->NewStringUTF(env, "vibrator");
    jobject svc = (*env)->CallObjectMethod(env, ctx, get, name);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    return svc;
}

static bool android_has_amplitude(JNIEnv* env, jobject vibrator, jclass vibrator_cls)
{
    jmethodID m = (*env)->GetMethodID(env, vibrator_cls, "hasAmplitudeControl", "()Z");
    if (!m)
    {
        (*env)->ExceptionClear(env);
        return false;
    }
    jboolean r = (*env)->CallBooleanMethod(env, vibrator, m);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return false;
    }
    return r == JNI_TRUE;
}

static u32 android_enumerate(void* user, Mel_Vib_Raw* out, u32 cap)
{
    (void)user;
    if (cap == 0)
        return 0;
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return 0;
    if ((*env)->PushLocalFrame(env, 16) != 0)
        return 0;

    u32     produced = 0;
    jobject ctx = android_context(env);
    jobject vibrator = ctx ? android_vibrator(env, ctx) : NULL;
    if (vibrator)
    {
        jclass    vc = (*env)->GetObjectClass(env, vibrator);
        jmethodID hasv = (*env)->GetMethodID(env, vc, "hasVibrator", "()Z");
        bool      present = true;
        if (hasv)
        {
            jboolean hv = (*env)->CallBooleanMethod(env, vibrator, hasv);
            present = (hv == JNI_TRUE);
        }
        else
            (*env)->ExceptionClear(env);

        if (present)
        {
            bool amp = android_has_amplitude(env, vibrator, vc);
            out[0] = (Mel_Vib_Raw){
                .stable_id = MEL_VIB_ANDROID_STABLE_ID,
                .name = S8("Android Vibrator"),
                .caps = {
                    .present = true,
                    .amplitude = amp,
                    .sharpness = false,
                    .envelopes = false,
                    .continuous = true,
                    .can_pause = true,
                    .pause_exact = false,
                    .completion_exact = false,
                    .actuator_count = 1,
                },
            };
            produced = 1;
        }
    }
    (*env)->PopLocalFrame(env, NULL);
    return produced;
}

static Mel_Vib_Status android_submit(void* user, u64 stable_id, u64 token, const Mel_Vib_Lowered* lowered, Mel_Vib_Completion completion)
{
    (void)user;
    (void)stable_id;
    (void)token;
    (void)completion;
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return MEL_VIB_ERROR;
    if (lowered->count == 0)
        return MEL_VIB_ERROR;
    if ((*env)->PushLocalFrame(env, 32) != 0)
        return MEL_VIB_ERROR;

    Mel_Vib_Status status = MEL_VIB_ERROR;
    jobject        ctx = android_context(env);
    jobject        vibrator = ctx ? android_vibrator(env, ctx) : NULL;
    if (vibrator)
    {
        jclass vc = (*env)->GetObjectClass(env, vibrator);
        bool   amp = android_has_amplitude(env, vibrator, vc);

        u32        nseg = lowered->count * 2;
        jlongArray jt = (*env)->NewLongArray(env, (jsize)nseg);
        jintArray  ja = (*env)->NewIntArray(env, (jsize)nseg);
        f32        prev_end = 0.0f;
        jsize      si = 0;
        for (u32 i = 0; i < lowered->count; i++)
        {
            Mel_Vib_Event e = lowered->events[i];
            f32           dur = e.duration > 0.0f ? e.duration : MEL_VIB_TRANSIENT_FLOOR_S;
            f32           gap = e.at - prev_end;
            if (gap < 0.0f)
                gap = 0.0f;
            jlong gms = (jlong)(gap * 1000.0f);
            jint  gz = 0;
            (*env)->SetLongArrayRegion(env, jt, si, 1, &gms);
            (*env)->SetIntArrayRegion(env, ja, si, 1, &gz);
            si++;
            jlong oms = (jlong)(dur * 1000.0f);
            jint  a = (jint)(e.intensity * 255.0f + 0.5f);
            if (a < 1)
                a = 1;
            if (a > 255)
                a = 255;
            (*env)->SetLongArrayRegion(env, jt, si, 1, &oms);
            (*env)->SetIntArrayRegion(env, ja, si, 1, &a);
            si++;
            prev_end = e.at + dur;
        }
        jint repeat = (lowered->loop == MEL_VIB_LOOP_FOREVER) ? 0 : -1;

        jclass  ve = (*env)->FindClass(env, "android/os/VibrationEffect");
        jobject effect = NULL;
        if (ve)
        {
            if (amp)
            {
                jmethodID cw = (*env)->GetStaticMethodID(env, ve, "createWaveform", "([J[II)Landroid/os/VibrationEffect;");
                if (cw)
                    effect = (*env)->CallStaticObjectMethod(env, ve, cw, jt, ja, repeat);
            }
            if (!effect)
            {
                (*env)->ExceptionClear(env);
                jmethodID cw2 = (*env)->GetStaticMethodID(env, ve, "createWaveform", "([JI)Landroid/os/VibrationEffect;");
                if (cw2)
                    effect = (*env)->CallStaticObjectMethod(env, ve, cw2, jt, repeat);
            }
        }
        else
            (*env)->ExceptionClear(env);

        if (effect)
        {
            jmethodID vib = (*env)->GetMethodID(env, vc, "vibrate", "(Landroid/os/VibrationEffect;)V");
            if (vib)
            {
                (*env)->CallVoidMethod(env, vibrator, vib, effect);
                if (!(*env)->ExceptionCheck(env))
                    status = MEL_VIB_OK;
                else
                    (*env)->ExceptionClear(env);
            }
        }
        else
        {
            (*env)->ExceptionClear(env);
            jmethodID vleg = (*env)->GetMethodID(env, vc, "vibrate", "([JI)V");
            if (vleg)
            {
                (*env)->CallVoidMethod(env, vibrator, vleg, jt, repeat);
                if (!(*env)->ExceptionCheck(env))
                    status = MEL_VIB_OK;
                else
                    (*env)->ExceptionClear(env);
            }
        }

        if (mel_vib_failed(status))
            mel_log_error("vibration", "android vibrate failed");
    }
    else
        mel_log_error("vibration", "android vibrator service unavailable");

    (*env)->PopLocalFrame(env, NULL);
    return status;
}

static void android_abort(void* user, u64 stable_id, u64 token)
{
    (void)user;
    (void)stable_id;
    (void)token;
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return;
    if ((*env)->PushLocalFrame(env, 16) != 0)
        return;
    jobject ctx = android_context(env);
    jobject vibrator = ctx ? android_vibrator(env, ctx) : NULL;
    if (vibrator)
    {
        jclass    vc = (*env)->GetObjectClass(env, vibrator);
        jmethodID cancel = (*env)->GetMethodID(env, vc, "cancel", "()V");
        if (cancel)
        {
            (*env)->CallVoidMethod(env, vibrator, cancel);
            if ((*env)->ExceptionCheck(env))
                (*env)->ExceptionClear(env);
        }
        else
            (*env)->ExceptionClear(env);
    }
    (*env)->PopLocalFrame(env, NULL);
}

void mel_vib__register_host_providers(void)
{
    static const Mel_Vib_Provider_Desc desc = {
        .name = "android-vibrator",
        .enumerate = android_enumerate,
        .submit = android_submit,
        .abort = android_abort,
    };
    mel_vib_provider_register(&desc);
}
