#include "android.h"

static jclass    s_cls;
static jmethodID s_create;
static jmethodID s_isChecked;

bool mel_gui__android_checkbox_register_jni(JNIEnv* env)
{
    jclass cls = (*env)->FindClass(env, "orgwall/melody/platform/MelCheckBox");
    if (!cls) return false;
    s_cls = (jclass)(*env)->NewGlobalRef(env, cls);
    (*env)->DeleteLocalRef(env, cls);

    s_create    = (*env)->GetStaticMethodID(env, s_cls, "create",
        "(JJIIIILjava/lang/String;Z)Landroid/view/View;");
    s_isChecked = (*env)->GetStaticMethodID(env, s_cls, "isChecked", "(J)Z");
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return false; }
    return s_create != NULL && s_isChecked != NULL;
}

void mel_gui__backend_checkbox_create(Mel_Gui_Widget* w, str8 text)
{
    JNIEnv* env = mel_gui__android_env();
    if (!env) return;

    Mel_Gui_CheckBox_Impl* impl = (Mel_Gui_CheckBox_Impl*)w->impl;
    jboolean initial = (impl && impl->initial_checked) ? JNI_TRUE : JNI_FALSE;

    jstring s = mel_gui__android_jstring(env, text);
    jobject view = (*env)->CallStaticObjectMethod(env, s_cls, s_create,
        mel_gui__android_pack(w->self),
        mel_gui__android_pack(w->parent),
        w->x, w->y, w->width, w->height, s, initial);
    (*env)->DeleteLocalRef(env, s);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return; }

    if (view) {
        w->native = (*env)->NewGlobalRef(env, view);
        (*env)->DeleteLocalRef(env, view);
    }
}

bool mel_gui__backend_checkbox_checked(Mel_Gui_Widget* w)
{
    if (!w || !w->native) return false;
    JNIEnv* env = mel_gui__android_env();
    if (!env) return false;
    return (bool)(*env)->CallStaticBooleanMethod(env, s_cls, s_isChecked,
        mel_gui__android_pack(w->self));
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelCheckBox_nativeToggled(JNIEnv* env, jclass cls, jlong h, jboolean checked)
{
    (void)env; (void)cls;
    Mel_Gui_Handle handle = mel_gui__android_unpack(h);
    Mel_Gui_Widget* w = mel_gui__get(handle);
    if (!w) return;
    Mel_Gui_CheckBox_Impl* impl = (Mel_Gui_CheckBox_Impl*)w->impl;
    if (impl && impl->on_.on_toggled) {
        impl->on_.on_toggled(handle, (bool)checked, w->user);
    }
}
