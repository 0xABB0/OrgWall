#include "android.h"

#define MEL_ANDROID_MEASURE_UNSPECIFIED 0

static jclass    s_cls;
static jmethodID s_lower; /* (Landroid/view/View;ZIII)Landroid/view/View; */

bool mel_gui__android_layout_register_jni(JNIEnv* env)
{
    jclass cls = (*env)->FindClass(env, "orgwall/melody/platform/MelLayout");
    if (!cls)
        return false;
    s_cls = (jclass)(*env)->NewGlobalRef(env, cls);
    (*env)->DeleteLocalRef(env, cls);

    s_lower = (*env)->GetStaticMethodID(env, s_cls, "lower", "(Landroid/view/View;ZIII)Landroid/view/View;");
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return false;
    }
    return s_lower != NULL;
}

bool mel_gui__backend_layout_adopt(Mel_Gui_Node* n, Mel_Layout* layout)
{
    if (!n || !layout || layout->cls != mel_linear_layout_class())
        return false;

    jobject host = (jobject)(n->content ? n->content : n->native);
    if (!host)
        return false;

    JNIEnv* env = mel_gui__android_env();
    if (!env)
        return false;

    const Mel_Linear_Layout* lin = (const Mel_Linear_Layout*)layout;

    jobject ll = (*env)->CallStaticObjectMethod(env, s_cls, s_lower, host, (jboolean)lin->vertical, (jint)mel_gui__android_dp2px(lin->spacing), (jint)mel_gui__android_dp2px(lin->margin), (jint)lin->cross_align);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return false;
    }
    if (!ll)
        return false;

    if (n->content)
        (*env)->DeleteGlobalRef(env, (jobject)n->content);
    n->content = (*env)->NewGlobalRef(env, ll);
    (*env)->DeleteLocalRef(env, ll);

    /* Children created before adoption were moved into the LinearLayout with
     * placeholder params; re-issue theirs from the layoutable data. */
    for (Mel_Gui_Handle c = mel_gui__first_child(n->self); !mel_gui_handle_is_none(c); c = mel_gui__next_sibling(c))
    {
        Mel_Gui_Node* cn = mel_gui__node(c);
        if (!cn || !cn->native)
            continue;
        jobject lp = mel_gui__android_linear_params(env, lin, cn);
        if (!lp)
            continue;
        (*env)->CallVoidMethod(env, (jobject)cn->native, mel_gui__android()->view_setLayoutParams, lp);
        (*env)->DeleteLocalRef(env, lp);
    }
    if ((*env)->ExceptionCheck(env))
        (*env)->ExceptionClear(env);

    return true;
}

bool mel_gui__backend_natural_size(Mel_Gui_Node* n, i32* out_w, i32* out_h)
{
    if (!n || !n->native || mel_gui__is_toplevel(n))
        return false;

    JNIEnv* env = mel_gui__android_env();
    if (!env)
        return false;

    Mel_Gui_Android* a = mel_gui__android();
    jobject          view = (jobject)n->native;

    (*env)->CallVoidMethod(env, view, a->view_measure, MEL_ANDROID_MEASURE_UNSPECIFIED, MEL_ANDROID_MEASURE_UNSPECIFIED);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return false;
    }

    jint mw = (*env)->CallIntMethod(env, view, a->view_getMeasuredWidth);
    jint mh = (*env)->CallIntMethod(env, view, a->view_getMeasuredHeight);
    if (mw <= 0 && mh <= 0)
        return false;

    if (out_w)
        *out_w = mel_gui__android_px2dp((int)mw);
    if (out_h)
        *out_h = mel_gui__android_px2dp((int)mh);
    return true;
}
