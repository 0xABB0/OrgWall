#include <gui.platform.android/gui.platform.android.h>
#include <gui/gui.h>
#include <gui.platform/gui.platform.h>

#include <allocator.heap/heap.h>
#include <string/str8.h>

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

    mel_gui_dispatch_app_message(MEL_GUI_MSG_APP_CREATE, 0, 0);
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_MelodyActivity_nativeAppResume(JNIEnv* env, jclass cls)
{
    MEL_UNUSED(env); MEL_UNUSED(cls);
    mel_gui_dispatch_app_message(MEL_GUI_MSG_APP_RESUME, 0, 0);
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_MelodyActivity_nativeAppPause(JNIEnv* env, jclass cls)
{
    MEL_UNUSED(env); MEL_UNUSED(cls);
    mel_gui_dispatch_app_message(MEL_GUI_MSG_APP_PAUSE, 0, 0);
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_MelodyActivity_nativeAppDestroy(JNIEnv* env, jclass cls)
{
    MEL_UNUSED(env); MEL_UNUSED(cls);
    mel_gui_dispatch_app_message(MEL_GUI_MSG_APP_DESTROY, 0, 0);
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_MelodyActivity_nativeAppBack(JNIEnv* env, jclass cls)
{
    MEL_UNUSED(env); MEL_UNUSED(cls);
    mel_gui_dispatch_app_message(MEL_GUI_MSG_APP_BACK, 0, 0);
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_MelodyActivity_nativeAppConfigChanged(JNIEnv* env, jclass cls)
{
    MEL_UNUSED(env); MEL_UNUSED(cls);
    mel_gui_dispatch_app_message(MEL_GUI_MSG_APP_CONFIG_CHANGED, 0, 0);
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_NativeGuiHost_dispatchClick(
    JNIEnv* env, jclass cls, jlong handle)
{
    MEL_UNUSED(env);
    MEL_UNUSED(cls);
    mel_gui_send_message(mel__unpack_handle(handle), MEL_GUI_MSG_CLICK, 0, 0);
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_NativeGuiHost_dispatchValueChanged(
    JNIEnv* env, jclass cls, jlong handle, jint value)
{
    MEL_UNUSED(env);
    MEL_UNUSED(cls);
    mel_gui_send_message(mel__unpack_handle(handle), MEL_GUI_MSG_VALUE_CHANGED, (Mel_Gui_WParam)(i64)value, 0);
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_NativeGuiHost_dispatchTextChanged(
    JNIEnv* env, jclass cls, jlong handle, jstring value)
{
    MEL_UNUSED(cls);
    str8 text = mel_gui_android_str8(env, value, mel_alloc_heap());
    mel_gui_send_message(mel__unpack_handle(handle), MEL_GUI_MSG_TEXT_CHANGED, (Mel_Gui_WParam)(usize)text.len, (Mel_Gui_LParam)(intptr_t)text.data);
    mel_gui_android_str8_free(env, text, mel_alloc_heap());
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_NativeGuiHost_dispatchFocus(
    JNIEnv* env, jclass cls, jlong handle, jboolean focused)
{
    MEL_UNUSED(env);
    MEL_UNUSED(cls);
    if (focused == JNI_TRUE) {
        mel_gui_dispatch_focus(mel__unpack_handle(handle));
    }
}

JNIEXPORT jboolean JNICALL Java_orgwall_melody_platform_NativeGuiHost_dispatchKey(
    JNIEnv* env, jclass cls, jlong handle, jint keyCode, jboolean down)
{
    MEL_UNUSED(env);
    MEL_UNUSED(cls);
    Mel_Gui_Msg msg = (down == JNI_TRUE) ? MEL_GUI_MSG_KEY_DOWN : MEL_GUI_MSG_KEY_UP;
    mel_gui_send_message(mel__unpack_handle(handle), msg, (Mel_Gui_WParam)(u32)keyCode, 0);
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL Java_orgwall_melody_platform_NativeGuiHost_dispatchPointer(
    JNIEnv* env, jclass cls, jlong handle, jint action, jint x, jint y)
{
    MEL_UNUSED(env);
    MEL_UNUSED(cls);
    Mel_Gui_Msg msg;
    switch (action) {
        case 0: msg = MEL_GUI_MSG_POINTER_DOWN; break;
        case 1: msg = MEL_GUI_MSG_POINTER_UP;   break;
        case 2: msg = MEL_GUI_MSG_POINTER_MOVE; break;
        case 3: msg = MEL_GUI_MSG_POINTER_UP;   break;
        default: return JNI_FALSE;
    }
    mel_gui_send_message(mel__unpack_handle(handle), msg, 1u, mel_gui_pack_xy(x, y));
    return JNI_TRUE;
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_NativeGuiHost_dispatchPaint(
    JNIEnv* env, jclass cls, jlong handle, jobject canvas)
{
    MEL_UNUSED(env);
    MEL_UNUSED(cls);
    mel_gui_send_message(mel__unpack_handle(handle), MEL_GUI_MSG_PAINT, 0, (Mel_Gui_LParam)(intptr_t)canvas);
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_NativeGuiHost_nativeDispatchPosted(
    JNIEnv* env, jclass cls, jlong handle, jint msg, jlong wparam, jlong lparam)
{
    MEL_UNUSED(env);
    MEL_UNUSED(cls);
    mel_gui_send_message(mel__unpack_handle(handle), (Mel_Gui_Msg)(u32)msg, (Mel_Gui_WParam)(u64)wparam, (Mel_Gui_LParam)lparam);
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_NativeGuiHost_nativeStartActivity(
    JNIEnv* env, jclass cls, jstring name)
{
    MEL_UNUSED(cls);
    str8 s = mel_gui_android_str8(env, name, mel_alloc_heap());
    mel_gui_destroy_all_roots();
    mel_gui_app_build_activity(s);
    mel_gui_android_str8_free(env, s, mel_alloc_heap());
}
