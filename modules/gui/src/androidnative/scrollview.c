#include "android.h"

static jclass    s_cls;
static jmethodID s_create;
static jmethodID s_content;
static jmethodID s_set_content_size;

bool mel_gui__android_scrollview_register_jni(JNIEnv* env)
{
    jclass cls = (*env)->FindClass(env, "orgwall/melody/platform/MelScrollView");
    if (!cls)
        return false;
    s_cls = (jclass)(*env)->NewGlobalRef(env, cls);
    (*env)->DeleteLocalRef(env, cls);

    s_create = (*env)->GetStaticMethodID(env, s_cls, "create", "(II)Landroid/view/View;");
    s_content = (*env)->GetStaticMethodID(env, s_cls, "content", "(Landroid/view/View;)Landroid/view/View;");
    s_set_content_size = (*env)->GetStaticMethodID(env, s_cls, "setContentSize", "(Landroid/view/View;II)V");
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return false;
    }
    return s_create && s_content && s_set_content_size;
}

Mel_Gui_Handle mel_scrollview_create_opt(Mel_Gui_Handle parent, Mel_ScrollView_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden, &o.layoutable, o.layout);
    Mel_Gui_Node*  n = mel_gui__node(h);
    if (!n)
        return h;

    n->is_scroll_host = true;
    n->content_floor_w = o.content_w > 0 ? o.content_w : 0;
    n->content_floor_h = o.content_h > 0 ? o.content_h : 0;

    JNIEnv* env = mel_gui__android_env();
    if (!env)
        return h;

    jint cw = o.content_w > 0 ? (jint)mel_gui__android_dp2px(o.content_w) : 0;
    jint ch = o.content_h > 0 ? (jint)mel_gui__android_dp2px(o.content_h) : 0;

    jobject view = (*env)->CallStaticObjectMethod(env, s_cls, s_create, cw, ch);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return h;
    }
    if (!view)
        return h;

    mel_gui__android_attach(n, view);
    mel_gui__android_install_focus(env, view, h, o.focus);

    jobject inner = (*env)->CallStaticObjectMethod(env, s_cls, s_content, view);
    if (inner)
    {
        n->content = (*env)->NewGlobalRef(env, inner);
        (*env)->DeleteLocalRef(env, inner);
    }

    (*env)->DeleteLocalRef(env, view);
    return h;
}

void mel_gui__backend_set_content_size(Mel_Gui_Node* n, i32 w, i32 h)
{
    if (!n || !n->content)
        return;
    JNIEnv* env = mel_gui__android_env();
    if (!env)
        return;
    jint wpx = (jint)mel_gui__android_dp2px(w);
    jint hpx = (jint)mel_gui__android_dp2px(h);
    (*env)->CallStaticVoidMethod(env, s_cls, s_set_content_size, (jobject)n->content, wpx, hpx);
    if ((*env)->ExceptionCheck(env))
        (*env)->ExceptionClear(env);
}
