#include "android.h"

#include <stdint.h>

static jclass    s_cls;
static jmethodID s_create;
static jmethodID s_close;

bool mel_gui__android_dialog_register_jni(JNIEnv* env)
{
    jclass cls = (*env)->FindClass(env, "orgwall/melody/platform/MelDialog");
    if (!cls) return false;
    s_cls = (jclass)(*env)->NewGlobalRef(env, cls);
    (*env)->DeleteLocalRef(env, cls);

    s_create = (*env)->GetStaticMethodID(env, s_cls, "create", "(Ljava/lang/String;JJ)Landroid/view/View;");
    s_close  = (*env)->GetStaticMethodID(env, s_cls, "close",  "(JI)V");
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return false; }
    return s_create && s_close;
}

Mel_Gui_Handle mel_dialog_create_opt(Mel_Dialog_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(MEL_GUI_HANDLE_NONE, o.x, o.y, o.w, o.h, 0, o.user,
                                         false, NULL, o.layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    JNIEnv* env = mel_gui__android_env();
    if (!env) return h;

    jstring s = mel_gui__android_jstring(env, o.title);
    jobject content = (*env)->CallStaticObjectMethod(env, s_cls, s_create, s,
        mel_gui__android_pack(h), (jlong)(intptr_t)o.on_.on_result);
    (*env)->DeleteLocalRef(env, s);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return h; }
    if (!content) return h;

    n->native = (*env)->NewGlobalRef(env, content);
    (*env)->DeleteLocalRef(env, content);

    mel_gui__frames_inc();
    return h;
}

void mel_dialog_close(Mel_Gui_Handle dialog, i32 result)
{
    JNIEnv* env = mel_gui__android_env();
    if (!env) return;
    (*env)->CallStaticVoidMethod(env, s_cls, s_close, mel_gui__android_pack(dialog), (jint)result);
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelDialog_nativeResult(JNIEnv* env, jclass cls, jlong h, jlong fn, jint result)
{
    (void)env; (void)cls;
    Mel_Gui_Handle handle = mel_gui__android_unpack(h);
    if (fn) ((Mel_Cb_I32)(intptr_t)fn)(handle, (i32)result, mel_gui_user(handle));
    mel_gui__destroy_tree(handle);
}
