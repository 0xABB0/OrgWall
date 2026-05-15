#include <gui.platform.android/gui.platform.android.h>
#include <gui/gui.h>

#include <allocator.heap/heap.h>
#include <string/string.str8.h>

#include <jni.h>
#include <android/log.h>

#define MEL_ANDROID_LOG_TAG "Melody"
#define MEL_ANDROID_LOG(...) __android_log_print(ANDROID_LOG_INFO, MEL_ANDROID_LOG_TAG, __VA_ARGS__)

static Mel_Gui_Handle mel__unpack_handle(jlong v)
{
    u64 u = (u64)v;
    return (Mel_Gui_Handle){ .handle = { .index = (u32)(u & 0xFFFFFFFFu), .generation = (u32)(u >> 32) } };
}

static bool mel__attach_activity(JNIEnv* env, jobject host)
{
    JavaVM* vm = NULL;
    if ((*env)->GetJavaVM(env, &vm) != JNI_OK || vm == NULL) return false;
    if (!mel_gui_android_attach(vm, env, host)) return false;
    return mel_gui_init();
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_MelodyActivity_nativeBuildActivity(
    JNIEnv* env, jclass cls, jobject host, jstring activity_name)
{
    MEL_UNUSED(cls);

    if (!mel__attach_activity(env, host)) {
        MEL_ANDROID_LOG("attach failed");
        return;
    }

    str8 name = mel_gui_android_str8(env, activity_name, mel_alloc_heap());
    mel_gui_app_build_activity(name);
    mel_gui_android_str8_free(env, name, mel_alloc_heap());
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_MelodyActivity_nativeResumeActivity(
    JNIEnv* env, jclass cls, jobject host, jstring activity_name)
{
    MEL_UNUSED(cls);
    MEL_UNUSED(activity_name);
    mel__attach_activity(env, host);
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_NativeGuiHost_nativeDispatchClick(
    JNIEnv* env, jclass cls, jlong handle)
{
    MEL_UNUSED(env);
    MEL_UNUSED(cls);
    mel_gui_send_message(mel__unpack_handle(handle), MEL_GUI_MSG_CLICK, 0, 0);
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_NativeGuiHost_nativeDispatchValueChanged(
    JNIEnv* env, jclass cls, jlong handle, jint value)
{
    MEL_UNUSED(env);
    MEL_UNUSED(cls);
    mel_gui_send_message(mel__unpack_handle(handle), MEL_GUI_MSG_VALUE_CHANGED, (Mel_Gui_WParam)(i64)value, 0);
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_NativeGuiHost_nativeDispatchTextChanged(
    JNIEnv* env, jclass cls, jlong handle, jstring value)
{
    MEL_UNUSED(cls);
    str8 text = mel_gui_android_str8(env, value, mel_alloc_heap());
    mel_gui_send_message(mel__unpack_handle(handle), MEL_GUI_MSG_TEXT_CHANGED, (Mel_Gui_WParam)(usize)text.len, (Mel_Gui_LParam)(intptr_t)text.data);
    mel_gui_android_str8_free(env, text, mel_alloc_heap());
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_NativeGuiHost_nativeDispatchPosted(
    JNIEnv* env, jclass cls, jlong handle, jint msg, jlong wparam, jlong lparam)
{
    MEL_UNUSED(env);
    MEL_UNUSED(cls);
    mel_gui_send_message(mel__unpack_handle(handle), (Mel_Gui_Msg)(u32)msg, (Mel_Gui_WParam)(u64)wparam, (Mel_Gui_LParam)lparam);
}
