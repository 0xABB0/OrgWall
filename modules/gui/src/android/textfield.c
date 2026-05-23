#include "android.h"

#include <string.h>

static jclass    s_cls;
static jmethodID s_create;

bool mel_gui__android_textfield_register_jni(JNIEnv* env)
{
    jclass cls = (*env)->FindClass(env, "orgwall/melody/platform/MelTextField");
    if (!cls) return false;
    s_cls = (jclass)(*env)->NewGlobalRef(env, cls);
    (*env)->DeleteLocalRef(env, cls);

    s_create = (*env)->GetStaticMethodID(env, s_cls, "create",
        "(JJIIIILjava/lang/String;)Landroid/view/View;");
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return false; }
    return s_create != NULL;
}

void mel_gui__backend_textfield_create(Mel_Gui_Widget* w, str8 text)
{
    JNIEnv* env = mel_gui__android_env();
    if (!env) return;

    jstring s = mel_gui__android_jstring(env, text);
    jobject view = (*env)->CallStaticObjectMethod(env, s_cls, s_create,
        mel_gui__android_pack(w->self),
        mel_gui__android_pack(w->parent),
        w->x, w->y, w->width, w->height, s);
    (*env)->DeleteLocalRef(env, s);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return; }

    if (view) {
        w->native = (*env)->NewGlobalRef(env, view);
        (*env)->DeleteLocalRef(env, view);
    }
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelTextField_nativeTextChanged(JNIEnv* env, jclass cls, jlong h, jstring text)
{
    (void)cls;
    Mel_Gui_Handle handle = mel_gui__android_unpack(h);
    Mel_Gui_Widget* w = mel_gui__get(handle);
    if (!w) return;
    Mel_Gui_TextField_Impl* impl = (Mel_Gui_TextField_Impl*)w->impl;
    if (!impl || !impl->on_.on_text_changed) return;

    const char* c = text ? (*env)->GetStringUTFChars(env, text, NULL) : NULL;
    str8 s = { (u8*)c, c ? (size)strlen(c) : 0 };
    impl->on_.on_text_changed(handle, s, w->user);
    if (c) (*env)->ReleaseStringUTFChars(env, text, c);
}
