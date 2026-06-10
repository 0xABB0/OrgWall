#include "android.h"

static jclass    s_cls;
static jmethodID s_create;

bool mel_gui__android_panel_register_jni(JNIEnv* env)
{
    jclass cls = (*env)->FindClass(env, "orgwall/melody/platform/MelPanel");
    if (!cls)
        return false;
    s_cls = (jclass)(*env)->NewGlobalRef(env, cls);
    (*env)->DeleteLocalRef(env, cls);

    s_create = (*env)->GetStaticMethodID(env, s_cls, "create", "()Landroid/view/View;");
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return false;
    }
    return s_create != NULL;
}

Mel_Gui_Handle mel_gui__screen_new(Mel_Gui_Handle window)
{
    Mel_Gui_Handle h = mel_panel_create(window, .x = 0, .y = 0, .w = 0, .h = 0);
    Mel_Gui_Node*  n = mel_gui__node(h);
    if (n)
        n->is_screen = true;
    return h;
}

Mel_Gui_Handle mel_panel_create_opt(Mel_Gui_Handle parent, Mel_Panel_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden, &o.layoutable, o.layout);
    Mel_Gui_Node*  n = mel_gui__node(h);
    if (!n)
        return h;

    JNIEnv* env = mel_gui__android_env();
    if (!env)
        return h;

    jobject view = (*env)->CallStaticObjectMethod(env, s_cls, s_create);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return h;
    }
    if (!view)
        return h;

    mel_gui__android_attach(n, view);
    mel_gui__android_install_focus(env, view, h, o.focus);

    (*env)->DeleteLocalRef(env, view);

    mel_gui__node_native_ready(h);
    if (mel_style_any(&o.style))
        mel_gui_set_style(h, o.style);
    return h;
}
