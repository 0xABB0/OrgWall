#include <gui.platform.android/gui.platform.android.h>

#include <gui.app/gui.app.h>
#include <gui/gui.platform.h>
#include <stdint.h>

static JavaVM* mel__gui_vm;
static jobject mel__gui_host;
static jclass mel__gui_host_class;
static jmethodID mel__gui_create;
static jmethodID mel__gui_bind_native_handle;
static jmethodID mel__gui_set_text;
static jmethodID mel__gui_set_rect;
static jmethodID mel__gui_show;
static jmethodID mel__gui_enable;
static jmethodID mel__gui_start_activity;

static JNIEnv* mel__gui_android_env(void)
{
    JNIEnv* env = NULL;
    if ((*mel__gui_vm)->GetEnv(mel__gui_vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        (*mel__gui_vm)->AttachCurrentThread(mel__gui_vm, &env, NULL);
    }
    return env;
}

static jstring mel__gui_android_jstring_from_str8(JNIEnv* env, str8 s)
{
    char buf[512];
    size n = str8_to_buf(s, buf, (size)sizeof(buf) - 1);
    buf[n] = 0;
    return (*env)->NewStringUTF(env, buf);
}

bool mel_gui_android_attach(JavaVM* vm, JNIEnv* env, jobject host)
{
    if (vm == NULL || env == NULL || host == NULL) return false;

    mel__gui_vm = vm;

    if (mel__gui_host != NULL) {
        (*env)->DeleteGlobalRef(env, mel__gui_host);
        mel__gui_host = NULL;
    }
    if (mel__gui_host_class != NULL) {
        (*env)->DeleteGlobalRef(env, mel__gui_host_class);
        mel__gui_host_class = NULL;
    }

    mel__gui_host = (*env)->NewGlobalRef(env, host);
    jclass local_host_class = (*env)->GetObjectClass(env, host);
    mel__gui_host_class = (*env)->NewGlobalRef(env, local_host_class);
    (*env)->DeleteLocalRef(env, local_host_class);

    mel__gui_create = (*env)->GetMethodID(
        env,
        mel__gui_host_class,
        "create",
        "(Ljava/lang/String;Ljava/lang/String;IIIIJI)Landroid/view/View;");
    mel__gui_bind_native_handle = (*env)->GetMethodID(env, mel__gui_host_class, "bindNativeHandle", "(Landroid/view/View;J)V");
    mel__gui_set_text = (*env)->GetMethodID(env, mel__gui_host_class, "setText", "(Landroid/view/View;Ljava/lang/String;)V");
    mel__gui_set_rect = (*env)->GetMethodID(env, mel__gui_host_class, "setRect", "(Landroid/view/View;IIII)V");
    mel__gui_show = (*env)->GetMethodID(env, mel__gui_host_class, "show", "(Landroid/view/View;Z)V");
    mel__gui_enable = (*env)->GetMethodID(env, mel__gui_host_class, "enable", "(Landroid/view/View;Z)V");
    mel__gui_start_activity = (*env)->GetMethodID(env, mel__gui_host_class, "startActivity", "(Ljava/lang/String;)V");

    return mel__gui_create != NULL &&
           mel__gui_bind_native_handle != NULL &&
           mel__gui_set_text != NULL &&
           mel__gui_set_rect != NULL &&
           mel__gui_show != NULL &&
           mel__gui_enable != NULL &&
           mel__gui_start_activity != NULL;
}

bool mel_gui_platform_init(void)
{
    return mel__gui_host != NULL;
}

void mel_gui_platform_shutdown(void)
{
}

bool mel_gui_platform_realize(Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc, str8 platform_class_name)
{
    if (h == NULL || desc == NULL || mel__gui_host == NULL) return false;

    JNIEnv* env = mel__gui_android_env();
    jstring class_name = mel__gui_android_jstring_from_str8(env, platform_class_name);
    jstring text = mel__gui_android_jstring_from_str8(env, desc->text);
    jobject local_view = (*env)->CallObjectMethod(
        env,
        mel__gui_host,
        mel__gui_create,
        class_name,
        text,
        desc->x,
        desc->y,
        desc->w,
        desc->h,
        (jlong)(intptr_t)desc->parent,
        (jint)desc->id);

    (*env)->DeleteLocalRef(env, class_name);
    (*env)->DeleteLocalRef(env, text);

    if (local_view == NULL) return false;

    jobject global_view = (*env)->NewGlobalRef(env, local_view);
    mel_gui__set_platform_handle(h, global_view);
    (*env)->CallVoidMethod(env, mel__gui_host, mel__gui_bind_native_handle, local_view, (jlong)(intptr_t)h);
    (*env)->DeleteLocalRef(env, local_view);
    return true;
}

void mel_gui_platform_destroy(Mel_Gui_Handle h)
{
    if (h == NULL) return;

    JNIEnv* env = mel__gui_android_env();
    jobject view = (jobject)mel_gui__platform_handle(h);
    if (view != NULL) {
        (*env)->DeleteGlobalRef(env, view);
        mel_gui__set_platform_handle(h, NULL);
    }
}

void mel_gui_platform_show(Mel_Gui_Handle h, bool visible)
{
    if (h == NULL) return;
    JNIEnv* env = mel__gui_android_env();
    (*env)->CallVoidMethod(env, mel__gui_host, mel__gui_show, (jobject)mel_gui__platform_handle(h), visible);
}

void mel_gui_platform_enable(Mel_Gui_Handle h, bool enabled)
{
    if (h == NULL) return;
    JNIEnv* env = mel__gui_android_env();
    (*env)->CallVoidMethod(env, mel__gui_host, mel__gui_enable, (jobject)mel_gui__platform_handle(h), enabled);
}

void mel_gui_platform_set_text(Mel_Gui_Handle h, str8 text)
{
    if (h == NULL) return;
    JNIEnv* env = mel__gui_android_env();
    jstring jtext = mel__gui_android_jstring_from_str8(env, text);
    (*env)->CallVoidMethod(env, mel__gui_host, mel__gui_set_text, (jobject)mel_gui__platform_handle(h), jtext);
    (*env)->DeleteLocalRef(env, jtext);
}

void mel_gui_platform_set_rect(Mel_Gui_Handle h, i32 x, i32 y, i32 width, i32 height)
{
    if (h == NULL) return;
    JNIEnv* env = mel__gui_android_env();
    (*env)->CallVoidMethod(env, mel__gui_host, mel__gui_set_rect, (jobject)mel_gui__platform_handle(h), x, y, width, height);
}

bool mel_gui_app_start_activity(str8 activity_name)
{
    if (mel__gui_host == NULL || mel__gui_start_activity == NULL) return false;

    JNIEnv* env = mel__gui_android_env();
    jstring jactivity_name = mel__gui_android_jstring_from_str8(env, activity_name);
    (*env)->CallVoidMethod(env, mel__gui_host, mel__gui_start_activity, jactivity_name);
    (*env)->DeleteLocalRef(env, jactivity_name);
    return true;
}

void mel_gui_android_dispatch_click(Mel_Gui_Handle h)
{
    if (h == NULL) return;
    mel_gui_send_message(h, MEL_GUI_MSG_CLICK, 0, 0);
    if (mel_gui_parent(h) != NULL) {
        mel_gui_send_message(mel_gui_parent(h), MEL_GUI_MSG_COMMAND, (Mel_Gui_WParam)(intptr_t)h, mel_gui_id(h));
    }
}

void mel_gui_android_dispatch_value_changed(Mel_Gui_Handle h, i32 value)
{
    if (h == NULL) return;
    mel_gui_send_message(h, MEL_GUI_MSG_VALUE_CHANGED, (Mel_Gui_WParam)value, 0);
    if (mel_gui_parent(h) != NULL) {
        mel_gui_send_message(mel_gui_parent(h), MEL_GUI_MSG_COMMAND, (Mel_Gui_WParam)(intptr_t)h, mel_gui_id(h));
    }
}

void mel_gui_android_dispatch_text_changed(Mel_Gui_Handle h, str8 text)
{
    if (h == NULL) return;
    mel_gui_send_message(h, MEL_GUI_MSG_TEXT_CHANGED, 0, (Mel_Gui_LParam)(intptr_t)&text);
    if (mel_gui_parent(h) != NULL) {
        mel_gui_send_message(mel_gui_parent(h), MEL_GUI_MSG_COMMAND, (Mel_Gui_WParam)(intptr_t)h, mel_gui_id(h));
    }
}
