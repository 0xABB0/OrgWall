#include "android.h"

#include <stdint.h>

static jclass    s_cls;
static jmethodID s_create;  /* (JLjava/lang/String;JIJ)Landroid/view/View; handle, title, fnResize, insetMode, fnInsets */
static jmethodID s_insets;  /* (Landroid/view/View;)[I  current insets (dp) */

bool mel_gui__android_frame_register_jni(JNIEnv* env)
{
    jclass cls = (*env)->FindClass(env, "orgwall/melody/platform/MelFrame");
    if (!cls) return false;
    s_cls = (jclass)(*env)->NewGlobalRef(env, cls);
    (*env)->DeleteLocalRef(env, cls);

    s_create = (*env)->GetStaticMethodID(env, s_cls, "create", "(JLjava/lang/String;JIJ)Landroid/view/View;");
    s_insets = (*env)->GetStaticMethodID(env, s_cls, "insets", "(Landroid/view/View;)[I");
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return false; }
    return s_create != NULL && s_insets != NULL;
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
        mel_gui__android_pack(h), s, (jlong)(intptr_t)o.lifecycle.on_resize,
        (jint)o.inset_mode, (jlong)(intptr_t)o.insets.on_insets_changed);
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

static Mel_Frame_Insets insets_from_dp(const jint v[20])
{
    return (Mel_Frame_Insets){
        .safe_area       = { v[0],  v[1],  v[2],  v[3]  },
        .system_bars     = { v[4],  v[5],  v[6],  v[7]  },
        .display_cutout  = { v[8],  v[9],  v[10], v[11] },
        .ime             = { v[12], v[13], v[14], v[15] },
        .system_gestures = { v[16], v[17], v[18], v[19] },
    };
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelFrame_nativeInsets(JNIEnv* env, jclass cls, jlong h, jlong fn, jintArray arr)
{
    (void)cls;
    if (!fn) return;
    Mel_Gui_Handle handle = mel_gui__android_unpack(h);

    jint v[20];
    (*env)->GetIntArrayRegion(env, arr, 0, 20, v);
    Mel_Frame_Insets in = insets_from_dp(v);
    ((Mel_Cb_Insets)(intptr_t)fn)(handle, &in, mel_gui_user(handle));
}

Mel_Frame_Insets mel_frame_insets(Mel_Gui_Handle h)
{
    Mel_Frame_Insets out = {0};
    Mel_Gui_Node*    n   = mel_gui__node(mel_gui__toplevel(h));
    if (!n || !n->native) return out;

    JNIEnv* env = mel_gui__android_env();
    if (!env) return out;

    jintArray arr = (jintArray)(*env)->CallStaticObjectMethod(env, s_cls, s_insets,
                                                              (jobject)n->native);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return out; }
    if (!arr) return out;

    jint v[20];
    (*env)->GetIntArrayRegion(env, arr, 0, 20, v);
    (*env)->DeleteLocalRef(env, arr);
    return insets_from_dp(v);
}
