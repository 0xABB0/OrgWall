#include <jni.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <android/log.h>
#include <gui.app/gui.app.h>
#include <gui.platform.android/gui.platform.android.h>

#define MEL_ANDROID_LOG_TAG "Melody"
#define MEL_ANDROID_LOG(...) __android_log_print(ANDROID_LOG_INFO, MEL_ANDROID_LOG_TAG, __VA_ARGS__)

static void jstring_to_buf(JNIEnv* env, jstring value, char* buf, size_t buf_size)
{
    if (buf_size == 0) return;
    buf[0] = 0;
    if (value == NULL) return;

    const char* chars = (*env)->GetStringUTFChars(env, value, NULL);
    if (chars != NULL) {
        snprintf(buf, buf_size, "%s", chars);
        (*env)->ReleaseStringUTFChars(env, value, chars);
    }
}

static bool attach_activity(JNIEnv* env, jobject host)
{
    JavaVM* vm = NULL;
    if ((*env)->GetJavaVM(env, &vm) != JNI_OK || vm == NULL) return false;
    if (!mel_gui_android_attach(vm, env, host)) return false;
    if (!mel_gui_init()) return false;
    return true;
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_MelodyActivity_nativeBuildActivity(
    JNIEnv* env, jclass cls, jobject host, jstring activity_name)
{
    MEL_UNUSED(cls);

    char name[64];
    jstring_to_buf(env, activity_name, name, sizeof(name));

    if (!attach_activity(env, host)) {
        MEL_ANDROID_LOG("failed to attach activity '%s'", name);
        return;
    }

    mel_gui_app_build_activity(str8_from_cstr(name));
    MEL_ANDROID_LOG("built activity '%s'", name);
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_MelodyActivity_nativeResumeActivity(
    JNIEnv* env, jclass cls, jobject host, jstring activity_name)
{
    MEL_UNUSED(cls);

    char name[64];
    jstring_to_buf(env, activity_name, name, sizeof(name));

    if (attach_activity(env, host)) {
        MEL_ANDROID_LOG("resumed activity '%s'", name);
    }
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_NativeGuiHost_nativeDispatchClick(
    JNIEnv* env, jclass cls, jlong handle)
{
    MEL_UNUSED(env);
    MEL_UNUSED(cls);
    mel_gui_android_dispatch_click((Mel_Gui_Handle)(intptr_t)handle);
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_NativeGuiHost_nativeDispatchValueChanged(
    JNIEnv* env, jclass cls, jlong handle, jint value)
{
    MEL_UNUSED(env);
    MEL_UNUSED(cls);
    mel_gui_android_dispatch_value_changed((Mel_Gui_Handle)(intptr_t)handle, value);
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_NativeGuiHost_nativeDispatchTextChanged(
    JNIEnv* env, jclass cls, jlong handle, jstring value)
{
    MEL_UNUSED(cls);
    char text_buf[128] = {0};
    jstring_to_buf(env, value, text_buf, sizeof(text_buf));

    mel_gui_android_dispatch_text_changed((Mel_Gui_Handle)(intptr_t)handle, str8_from_cstr(text_buf));
}
