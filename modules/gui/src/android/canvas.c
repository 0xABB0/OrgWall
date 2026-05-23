#include "android.h"

#include <stdint.h>

static jclass    s_cls;
static jmethodID s_create;  /* (JJJJJJJ)Landroid/view/View; handle + paint,down,move,up,keyDown,keyUp */

bool mel_gui__android_canvas_register_jni(JNIEnv* env)
{
    jclass cls = (*env)->FindClass(env, "orgwall/melody/platform/MelCanvas");
    if (!cls) return false;
    s_cls = (jclass)(*env)->NewGlobalRef(env, cls);
    (*env)->DeleteLocalRef(env, cls);

    s_create = (*env)->GetStaticMethodID(env, s_cls, "create", "(JJJJJJJ)Landroid/view/View;");
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return false; }
    return s_create != NULL;
}

Mel_Gui_Handle mel_canvas_create_opt(Mel_Gui_Handle parent, Mel_Canvas_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    JNIEnv* env = mel_gui__android_env();
    if (!env) return h;

    jobject view = (*env)->CallStaticObjectMethod(env, s_cls, s_create,
        mel_gui__android_pack(h),
        (jlong)(intptr_t)o.on_.on_paint,
        (jlong)(intptr_t)o.pointer.on_pointer_down,
        (jlong)(intptr_t)o.pointer.on_pointer_move,
        (jlong)(intptr_t)o.pointer.on_pointer_up,
        (jlong)(intptr_t)o.keyboard.on_key_down,
        (jlong)(intptr_t)o.keyboard.on_key_up);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return h; }
    if (!view) return h;

    mel_gui__android_attach(n, view);
    mel_gui__android_install_focus(env, view, h, o.focus);

    (*env)->DeleteLocalRef(env, view);
    return h;
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelCanvasView_nativePaint(JNIEnv* env, jclass cls, jlong h, jlong fn, jobject canvas, jint w, jint height)
{
    (void)env; (void)cls;
    if (!fn) return;
    Mel_Gui_Handle handle = mel_gui__android_unpack(h);
    ((Mel_Cb_Paint)(intptr_t)fn)(handle, (void*)canvas, (i32)w, (i32)height, mel_gui_user(handle));
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelCanvasView_nativePointer(JNIEnv* env, jclass cls, jlong h, jlong fn, jint x, jint y)
{
    (void)env; (void)cls;
    if (!fn) return;
    Mel_Gui_Handle handle = mel_gui__android_unpack(h);
    ((Mel_Cb_Point)(intptr_t)fn)(handle, (i32)x, (i32)y, mel_gui_user(handle));
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelCanvasView_nativeKey(JNIEnv* env, jclass cls, jlong h, jlong fn, jint key)
{
    (void)env; (void)cls;
    if (!fn) return;
    Mel_Gui_Handle handle = mel_gui__android_unpack(h);
    ((Mel_Cb_I32)(intptr_t)fn)(handle, (i32)key, mel_gui_user(handle));
}
