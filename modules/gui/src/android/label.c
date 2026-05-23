#include "android.h"

static jclass    s_cls;
static jmethodID s_create;

bool mel_gui__android_label_register_jni(JNIEnv* env)
{
    jclass cls = (*env)->FindClass(env, "orgwall/melody/platform/MelLabel");
    if (!cls) return false;
    s_cls = (jclass)(*env)->NewGlobalRef(env, cls);
    (*env)->DeleteLocalRef(env, cls);

    s_create = (*env)->GetStaticMethodID(env, s_cls, "create",
        "(JJIIIILjava/lang/String;)Landroid/view/View;");
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return false; }
    return s_create != NULL;
}

void mel_gui__backend_label_create(Mel_Gui_Widget* w, str8 text)
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
