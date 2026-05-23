#include "android.h"

#include <string.h>

static jclass    s_cls;
static jmethodID s_create;

bool mel_gui__android_frame_register_jni(JNIEnv* env)
{
    jclass cls = (*env)->FindClass(env, "orgwall/melody/platform/MelFrame");
    if (!cls) return false;
    s_cls = (jclass)(*env)->NewGlobalRef(env, cls);
    (*env)->DeleteLocalRef(env, cls);

    s_create = (*env)->GetStaticMethodID(env, s_cls, "create",
        "(JLjava/lang/String;)Landroid/view/View;");
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return false; }
    return s_create != NULL;
}

void mel_gui__backend_frame_create(Mel_Gui_Widget* w, str8 title)
{
    JNIEnv* env = mel_gui__android_env();
    if (!env) return;

    Mel_Gui_Frame_Impl* fi = (Mel_Gui_Frame_Impl*)w->impl;
    if (fi) {
        size n = title.len < (size)sizeof(fi->title) - 1 ? title.len : (size)sizeof(fi->title) - 1;
        if (n > 0 && title.data) memcpy(fi->title, title.data, (usize)n);
        fi->title[n] = 0;
    }

    jstring s = mel_gui__android_jstring(env, title);
    jobject view = (*env)->CallStaticObjectMethod(env, s_cls, s_create,
        mel_gui__android_pack(w->self), s);
    (*env)->DeleteLocalRef(env, s);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return; }

    if (view) {
        w->native = (*env)->NewGlobalRef(env, view);
        (*env)->DeleteLocalRef(env, view);
        mel_gui__frames_inc();
    }
}
