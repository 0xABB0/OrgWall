#include "android.h"

#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <gui/controls/gpu_view.h>

static jclass    s_cls;
static jmethodID s_create; /* (JJJJJJJ)Landroid/view/View; handle,resize,down,move,up,keyDown,keyUp */

// ANativeWindow can't be stored in node->content (the backend destroy path
// treats content as a Java global ref), so it lives in a small side table
// keyed by handle.
#define MEL_GPU_VIEW_MAX 16
static struct { Mel_Gui_Handle handle; ANativeWindow* window; } s_windows[MEL_GPU_VIEW_MAX];

static ANativeWindow** window_slot(Mel_Gui_Handle h)
{
    for (int i = 0; i < MEL_GPU_VIEW_MAX; i++)
        if (mel_gui_handle_eq(s_windows[i].handle, h)) return &s_windows[i].window;
    for (int i = 0; i < MEL_GPU_VIEW_MAX; i++)
        if (mel_gui_handle_is_none(s_windows[i].handle)) { s_windows[i].handle = h; return &s_windows[i].window; }
    return NULL;
}

bool mel_gui__android_gpu_view_register_jni(JNIEnv* env)
{
    jclass cls = (*env)->FindClass(env, "orgwall/melody/platform/MelGpu");
    if (!cls) return false;
    s_cls = (jclass)(*env)->NewGlobalRef(env, cls);
    (*env)->DeleteLocalRef(env, cls);

    s_create = (*env)->GetStaticMethodID(env, s_cls, "create", "(JJJJJJJ)Landroid/view/View;");
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return false; }
    return s_create != NULL;
}

Mel_Gui_Handle mel_gpu_view_create_opt(Mel_Gui_Handle parent, Mel_Gpu_View_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    JNIEnv* env = mel_gui__android_env();
    if (!env) return h;

    jobject view = (*env)->CallStaticObjectMethod(env, s_cls, s_create,
        mel_gui__android_pack(h),
        (jlong)(intptr_t)o.on_.on_resize,
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

void* mel_gpu_view_surface(Mel_Gui_Handle h)
{
    ANativeWindow** slot = window_slot(h);
    return slot ? *slot : NULL;
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelGpuView_nativeSurfaceChanged(JNIEnv* env, jclass cls, jlong h, jlong fn,
                                                             jobject surface, jint w, jint height)
{
    (void)cls;
    Mel_Gui_Handle handle = mel_gui__android_unpack(h);
    ANativeWindow** slot = window_slot(handle);
    if (slot) {
        if (*slot) ANativeWindow_release(*slot);
        *slot = surface ? ANativeWindow_fromSurface(env, surface) : NULL;
    }
    if (fn) ((Mel_Cb_Resize)(intptr_t)fn)(handle, (i32)w, (i32)height, mel_gui_user(handle));
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelGpuView_nativeSurfaceDestroyed(JNIEnv* env, jclass cls, jlong h, jlong fn)
{
    (void)env; (void)cls;
    Mel_Gui_Handle handle = mel_gui__android_unpack(h);
    // Tear down first (size 0,0 signals the host to drop the swapchain), then
    // release the window so the now-dead VkSurface is gone before the next frame.
    if (fn) ((Mel_Cb_Resize)(intptr_t)fn)(handle, 0, 0, mel_gui_user(handle));
    ANativeWindow** slot = window_slot(handle);
    if (slot && *slot) { ANativeWindow_release(*slot); *slot = NULL; }
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelGpuView_nativePointer(JNIEnv* env, jclass cls, jlong h, jlong fn, jint x, jint y)
{
    (void)env; (void)cls;
    if (!fn) return;
    Mel_Gui_Handle handle = mel_gui__android_unpack(h);
    ((Mel_Cb_Point)(intptr_t)fn)(handle, (i32)x, (i32)y, mel_gui_user(handle));
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelGpuView_nativeKey(JNIEnv* env, jclass cls, jlong h, jlong fn, jint key)
{
    (void)env; (void)cls;
    if (!fn) return;
    Mel_Gui_Handle handle = mel_gui__android_unpack(h);
    ((Mel_Cb_I32)(intptr_t)fn)(handle, (i32)key, mel_gui_user(handle));
}
