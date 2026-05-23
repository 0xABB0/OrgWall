#include "android.h"

static jclass    s_cls;
static jmethodID s_create;
static jmethodID s_value;
static jmethodID s_setValue;

bool mel_gui__android_slider_register_jni(JNIEnv* env)
{
    jclass cls = (*env)->FindClass(env, "orgwall/melody/platform/MelSlider");
    if (!cls) return false;
    s_cls = (jclass)(*env)->NewGlobalRef(env, cls);
    (*env)->DeleteLocalRef(env, cls);

    s_create   = (*env)->GetStaticMethodID(env, s_cls, "create",   "(JJIIIIIII)Landroid/view/View;");
    s_value    = (*env)->GetStaticMethodID(env, s_cls, "value",    "(J)I");
    s_setValue = (*env)->GetStaticMethodID(env, s_cls, "setValue", "(JI)V");
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return false; }
    return s_create != NULL && s_value != NULL && s_setValue != NULL;
}

void mel_gui__backend_slider_create(Mel_Gui_Widget* w, str8 text)
{
    (void)text;
    JNIEnv* env = mel_gui__android_env();
    if (!env) return;

    Mel_Gui_Slider_Impl* impl = (Mel_Gui_Slider_Impl*)w->impl;
    i32 mn  = impl ? impl->min_value : 0;
    i32 mx  = impl ? impl->max_value : 100;
    i32 val = impl ? impl->value     : 0;

    jobject view = (*env)->CallStaticObjectMethod(env, s_cls, s_create,
        mel_gui__android_pack(w->self),
        mel_gui__android_pack(w->parent),
        w->x, w->y, w->width, w->height,
        mn, mx, val);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return; }

    if (view) {
        w->native = (*env)->NewGlobalRef(env, view);
        (*env)->DeleteLocalRef(env, view);
    }
}

i32 mel_gui__backend_slider_value(Mel_Gui_Widget* w)
{
    if (!w || !w->native) return 0;
    JNIEnv* env = mel_gui__android_env();
    if (!env) return 0;
    Mel_Gui_Slider_Impl* impl = (Mel_Gui_Slider_Impl*)w->impl;
    i32 progress = (i32)(*env)->CallStaticIntMethod(env, s_cls, s_value,
        mel_gui__android_pack(w->self));
    return progress + (impl ? impl->min_value : 0);
}

void mel_gui__backend_slider_set_value(Mel_Gui_Widget* w, i32 value)
{
    if (!w || !w->native) return;
    JNIEnv* env = mel_gui__android_env();
    if (!env) return;
    Mel_Gui_Slider_Impl* impl = (Mel_Gui_Slider_Impl*)w->impl;
    if (impl) impl->value = value;
    (*env)->CallStaticVoidMethod(env, s_cls, s_setValue,
        mel_gui__android_pack(w->self), value - (impl ? impl->min_value : 0));
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelSlider_nativeValueChanged(JNIEnv* env, jclass cls, jlong h, jint progress)
{
    (void)env; (void)cls;
    Mel_Gui_Handle handle = mel_gui__android_unpack(h);
    Mel_Gui_Widget* w = mel_gui__get(handle);
    if (!w) return;
    Mel_Gui_Slider_Impl* impl = (Mel_Gui_Slider_Impl*)w->impl;
    i32 value = progress + (impl ? impl->min_value : 0);
    if (impl) impl->value = value;
    if (impl && impl->on_.on_value_changed) {
        impl->on_.on_value_changed(handle, value, w->user);
    }
}
