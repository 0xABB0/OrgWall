#include "android.h"

static jclass    s_cls;
static jmethodID s_create;

bool mel_gui__android_canvas_register_jni(JNIEnv* env)
{
    jclass cls = (*env)->FindClass(env, "orgwall/melody/platform/MelCanvas");
    if (!cls) return false;
    s_cls = (jclass)(*env)->NewGlobalRef(env, cls);
    (*env)->DeleteLocalRef(env, cls);

    s_create = (*env)->GetStaticMethodID(env, s_cls, "create",
        "(JJIIII)Landroid/view/View;");
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return false; }
    return s_create != NULL;
}

void mel_gui__backend_canvas_create(Mel_Gui_Widget* w, str8 text)
{
    (void)text;
    JNIEnv* env = mel_gui__android_env();
    if (!env) return;

    jobject view = (*env)->CallStaticObjectMethod(env, s_cls, s_create,
        mel_gui__android_pack(w->self),
        mel_gui__android_pack(w->parent),
        w->x, w->y, w->width, w->height);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return; }

    if (view) {
        w->native = (*env)->NewGlobalRef(env, view);
        (*env)->DeleteLocalRef(env, view);
    }
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelCanvasView_nativePaint(JNIEnv* env, jclass cls, jlong h, jobject canvas, jint w, jint height)
{
    (void)env; (void)cls;
    Mel_Gui_Handle handle = mel_gui__android_unpack(h);
    Mel_Gui_Widget* widget = mel_gui__get(handle);
    if (!widget) return;
    Mel_Gui_Canvas_Impl* impl = (Mel_Gui_Canvas_Impl*)widget->impl;
    if (impl && impl->on_.on_paint) {
        impl->on_.on_paint(handle, (void*)canvas, w, height, widget->user);
    }
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelCanvasView_nativePointer(JNIEnv* env, jclass cls, jlong h, jint kind, jint x, jint y)
{
    (void)env; (void)cls;
    Mel_Gui_Handle handle = mel_gui__android_unpack(h);
    switch (kind) {
        case 0: mel_gui__fire_pointer_down(handle, x, y); break;
        case 1: mel_gui__fire_pointer_move(handle, x, y); break;
        case 2: mel_gui__fire_pointer_up  (handle, x, y); break;
        default: break;
    }
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelCanvasView_nativeKey(JNIEnv* env, jclass cls, jlong h, jint key, jboolean down)
{
    (void)env; (void)cls;
    Mel_Gui_Handle handle = mel_gui__android_unpack(h);
    if (down) mel_gui__fire_key_down(handle, (Mel_Key)key);
    else      mel_gui__fire_key_up  (handle, (Mel_Key)key);
}
