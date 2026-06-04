#include <camera/android/android.h>

#include <platform/android/jni.h>
#include <log/log.h>

#define MEL_CAMERA_PERMISSION "android.permission.CAMERA"
#define MEL_CAMERA_PERMISSION_GRANTED 0

static jobject camera_android_context(JNIEnv* env)
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

bool mel_camera_android_permission_granted(void)
{
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return false;
    if ((*env)->PushLocalFrame(env, 8) < 0)
        return false;

    jobject ctx = camera_android_context(env);
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
    jstring perm = (*env)->NewStringUTF(env, MEL_CAMERA_PERMISSION);
    jint    res = (*env)->CallIntMethod(env, ctx, check, perm);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        (*env)->PopLocalFrame(env, NULL);
        return false;
    }
    (*env)->PopLocalFrame(env, NULL);
    return res == MEL_CAMERA_PERMISSION_GRANTED;
}

bool mel_camera_android_request_permission(void)
{
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return false;
    if ((*env)->PushLocalFrame(env, 4) < 0)
        return false;

    jclass cls = (*env)->FindClass(env, "orgwall/melody/camera/MelodyCamera");
    if (!cls)
    {
        (*env)->ExceptionClear(env);
        (*env)->PopLocalFrame(env, NULL);
        mel_log_error("camera", "camera2: MelodyCamera Java helper not found; cannot request permission");
        return false;
    }
    jmethodID req = (*env)->GetStaticMethodID(env, cls, "requestPermission", "()Z");
    if (!req)
    {
        (*env)->ExceptionClear(env);
        (*env)->PopLocalFrame(env, NULL);
        mel_log_error("camera", "camera2: MelodyCamera.requestPermission missing");
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

JNIEXPORT void JNICALL Java_orgwall_melody_camera_MelodyCamera_nativePermissionResult(JNIEnv* env, jclass cls, jboolean granted)
{
    (void)env;
    (void)cls;
    mel_camera_android_on_permission(granted == JNI_TRUE);
}
