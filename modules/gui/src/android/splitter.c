#include "android.h"

#include <math.h>
#include <stdint.h>

static jclass    s_cls;
static jmethodID s_create;
static jmethodID s_addPane;
static jmethodID s_view_getWidth;
static jmethodID s_view_getHeight;

static i32 px2dp(int px)
{
    float d = mel_gui__android()->density;
    if (d <= 0) d = 1.0f;
    return (i32)lroundf((float)px / d);
}

bool mel_gui__android_splitter_register_jni(JNIEnv* env)
{
    jclass cls = (*env)->FindClass(env, "orgwall/melody/platform/MelSplitter");
    if (!cls) return false;
    s_cls = (jclass)(*env)->NewGlobalRef(env, cls);
    (*env)->DeleteLocalRef(env, cls);

    s_create  = (*env)->GetStaticMethodID(env, s_cls, "create",  "(JZ)Landroid/view/View;");
    s_addPane = (*env)->GetStaticMethodID(env, s_cls, "addPane", "(Landroid/view/View;II)Landroid/view/View;");

    jclass view = (*env)->FindClass(env, "android/view/View");
    if (!view) return false;
    s_view_getWidth  = (*env)->GetMethodID(env, view, "getWidth",  "()I");
    s_view_getHeight = (*env)->GetMethodID(env, view, "getHeight", "()I");
    (*env)->DeleteLocalRef(env, view);

    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return false; }
    return s_create && s_addPane && s_view_getWidth && s_view_getHeight;
}

Mel_Gui_Handle mel_splitter_create_opt(Mel_Gui_Handle parent, Mel_Splitter_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    JNIEnv* env = mel_gui__android_env();
    if (!env) return h;

    jboolean vertical = (o.orientation == MEL_SPLIT_VERTICAL);
    jobject  view = (*env)->CallStaticObjectMethod(env, s_cls, s_create,
        mel_gui__android_pack(h), vertical);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return h; }
    if (!view) return h;

    mel_gui__android_attach(n, view);
    (*env)->DeleteLocalRef(env, view);
    return h;
}

Mel_Gui_Handle mel_splitpane_create_opt(Mel_Gui_Handle splitter, Mel_SplitPane_Opt o)
{
    Mel_Gui_Node* sn = mel_gui__node(splitter);
    if (!sn || !sn->native) return MEL_GUI_HANDLE_NONE;

    Mel_Gui_Handle h = mel_gui__node_new(splitter, 0, 0, 0, 0, o.id, o.user, false,
                                         &o.layoutable, o.layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    JNIEnv* env = mel_gui__android_env();
    if (!env) return h;

    jobject pane = (*env)->CallStaticObjectMethod(env, s_cls, s_addPane,
        (jobject)sn->native, (jint)o.min_size, (jint)o.initial_size);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return h; }
    if (!pane) return h;

    n->native = (*env)->NewGlobalRef(env, pane);
    (*env)->DeleteLocalRef(env, pane);
    return h;
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelSplitter_nativeLayout(JNIEnv* env, jclass cls, jlong h)
{
    (void)cls;
    Mel_Gui_Handle splitter = mel_gui__android_unpack(h);

    u32 count = 0;
    Mel_Gui_Node* data = mel_gui__nodes(&count);
    for (u32 i = 0; i < count; i++) {
        Mel_Gui_Node* c = &data[i];
        if (!mel_gui_handle_eq(c->parent, splitter) || !c->native) continue;

        jobject pane = (jobject)c->native;
        i32 pw = px2dp((*env)->CallIntMethod(env, pane, s_view_getWidth));
        i32 ph = px2dp((*env)->CallIntMethod(env, pane, s_view_getHeight));

        c->x = 0; c->y = 0;
        c->width = pw; c->height = ph;
        if (c->layout) mel_gui__layout_arrange(c->self);
    }
}
