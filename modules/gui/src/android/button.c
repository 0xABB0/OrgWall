#include "android.h"

#include <stdint.h>

static jclass    s_cls;
static jmethodID s_create;        /* (Ljava/lang/String;)Landroid/view/View; */
static jmethodID s_installClick;  /* (Landroid/view/View;JJ)V */

bool mel_gui__android_button_register_jni(JNIEnv* env)
{
    jclass cls = (*env)->FindClass(env, "orgwall/melody/platform/MelButton");
    if (!cls) return false;
    s_cls = (jclass)(*env)->NewGlobalRef(env, cls);
    (*env)->DeleteLocalRef(env, cls);

    s_create       = (*env)->GetStaticMethodID(env, s_cls, "create",       "(Ljava/lang/String;)Landroid/view/View;");
    s_installClick = (*env)->GetStaticMethodID(env, s_cls, "installClick", "(Landroid/view/View;JJ)V");
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return false; }
    return s_create && s_installClick;
}

Mel_Gui_Handle mel_button_create_opt(Mel_Gui_Handle parent, Mel_Button_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    JNIEnv* env = mel_gui__android_env();
    if (!env) return h;

    jstring s = mel_gui__android_jstring(env, o.text);
    jobject view = (*env)->CallStaticObjectMethod(env, s_cls, s_create, s);
    (*env)->DeleteLocalRef(env, s);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return h; }
    if (!view) return h;

    mel_gui__android_attach(n, view);

    if (o.pointer.on_click) {
        (*env)->CallStaticVoidMethod(env, s_cls, s_installClick, view,
            mel_gui__android_pack(h), (jlong)(intptr_t)o.pointer.on_click);
    }
    mel_gui__android_install_focus(env, view, h, o.focus);

    (*env)->DeleteLocalRef(env, view);
    return h;
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelButton_nativeClick(JNIEnv* env, jclass cls, jlong h, jlong fn)
{
    (void)env; (void)cls;
    if (!fn) return;
    Mel_Gui_Handle handle = mel_gui__android_unpack(h);
    ((Mel_Cb_Void)(intptr_t)fn)(handle, mel_gui_user(handle));
}
