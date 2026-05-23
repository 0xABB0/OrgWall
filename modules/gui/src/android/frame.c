#include "android.h"

#include <stdint.h>

static jclass    s_cls;
static jmethodID s_create;  /* (JLjava/lang/String;J)Landroid/view/View; handle, title, fnResize */

bool mel_gui__android_frame_register_jni(JNIEnv* env)
{
    jclass cls = (*env)->FindClass(env, "orgwall/melody/platform/MelFrame");
    if (!cls) return false;
    s_cls = (jclass)(*env)->NewGlobalRef(env, cls);
    (*env)->DeleteLocalRef(env, cls);

    s_create = (*env)->GetStaticMethodID(env, s_cls, "create", "(JLjava/lang/String;J)Landroid/view/View;");
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return false; }
    return s_create != NULL;
}

Mel_Gui_Handle mel_frame_create_opt(Mel_Frame_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(MEL_GUI_HANDLE_NONE, o.x, o.y, o.w, o.h, 0, o.user,
                                         o.initial_state == MEL_FRAME_HIDDEN, NULL, o.layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    JNIEnv* env = mel_gui__android_env();
    if (!env) return h;

    jstring s = mel_gui__android_jstring(env, o.title);
    jobject view = (*env)->CallStaticObjectMethod(env, s_cls, s_create,
        mel_gui__android_pack(h), s, (jlong)(intptr_t)o.lifecycle.on_resize);
    (*env)->DeleteLocalRef(env, s);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return h; }
    if (!view) return h;

    n->native = (*env)->NewGlobalRef(env, view);
    (*env)->DeleteLocalRef(env, view);
    mel_gui__frames_inc();
    return h;
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelFrame_nativeResize(JNIEnv* env, jclass cls, jlong h, jlong fn, jint w, jint height)
{
    (void)env; (void)cls;
    Mel_Gui_Handle handle = mel_gui__android_unpack(h);
    mel_gui__resized(handle, (i32)w, (i32)height);
    if (fn) ((Mel_Cb_Resize)(intptr_t)fn)(handle, (i32)w, (i32)height, mel_gui_user(handle));
}
