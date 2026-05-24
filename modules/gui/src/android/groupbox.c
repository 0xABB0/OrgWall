#include "android.h"

static jclass    s_cls;
static jmethodID s_create;
static jmethodID s_content;

bool mel_gui__android_groupbox_register_jni(JNIEnv* env)
{
    jclass cls = (*env)->FindClass(env, "orgwall/melody/platform/MelGroupBox");
    if (!cls) return false;
    s_cls = (jclass)(*env)->NewGlobalRef(env, cls);
    (*env)->DeleteLocalRef(env, cls);

    s_create  = (*env)->GetStaticMethodID(env, s_cls, "create",  "(Ljava/lang/String;)Landroid/view/View;");
    s_content = (*env)->GetStaticMethodID(env, s_cls, "content", "(Landroid/view/View;)Landroid/view/View;");
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return false; }
    return s_create && s_content;
}

Mel_Gui_Handle mel_groupbox_create_opt(Mel_Gui_Handle parent, Mel_GroupBox_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, o.layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    JNIEnv* env = mel_gui__android_env();
    if (!env) return h;

    jstring s    = mel_gui__android_jstring(env, o.title);
    jobject view = (*env)->CallStaticObjectMethod(env, s_cls, s_create, s);
    (*env)->DeleteLocalRef(env, s);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return h; }
    if (!view) return h;

    mel_gui__android_attach(n, view);
    mel_gui__android_install_focus(env, view, h, o.focus);

    jobject inner = (*env)->CallStaticObjectMethod(env, s_cls, s_content, view);
    if (inner) {
        n->content = (*env)->NewGlobalRef(env, inner);
        (*env)->DeleteLocalRef(env, inner);
    }

    (*env)->DeleteLocalRef(env, view);
    return h;
}
