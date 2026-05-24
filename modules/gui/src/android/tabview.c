#include "android.h"

#include <stdint.h>

#define TABSTRIP_DP 48

static jclass    s_cls;
static jmethodID s_create;
static jmethodID s_addTab;
static jmethodID s_select;
static jmethodID s_selected;

static void tab_arrange(Mel_Layout* layout, Mel_Gui_Handle container)
{
    (void)layout;
    Mel_Gui_Node* node = mel_gui__node(container);
    if (!node) return;

    i32 pw = node->width;
    i32 ph = node->height - TABSTRIP_DP;
    if (ph < 0) ph = 0;

    u32 count = 0;
    Mel_Gui_Node* data = mel_gui__nodes(&count);
    for (u32 i = 0; i < count; i++) {
        Mel_Gui_Node* c = &data[i];
        if (!mel_gui_handle_eq(c->parent, container)) continue;
        c->x = 0;
        c->y = 0;
        c->width  = pw;
        c->height = ph;
        if (c->layout) mel_gui__layout_arrange(c->self);
    }
}

static const Mel_Layout_Vtable s_tab_vtable = { .arrange = tab_arrange };

bool mel_gui__android_tabview_register_jni(JNIEnv* env)
{
    jclass cls = (*env)->FindClass(env, "orgwall/melody/platform/MelTabView");
    if (!cls) return false;
    s_cls = (jclass)(*env)->NewGlobalRef(env, cls);
    (*env)->DeleteLocalRef(env, cls);

    s_create   = (*env)->GetStaticMethodID(env, s_cls, "create",   "(JJ)Landroid/view/View;");
    s_addTab   = (*env)->GetStaticMethodID(env, s_cls, "addTab",   "(Landroid/view/View;Ljava/lang/String;)Landroid/view/View;");
    s_select   = (*env)->GetStaticMethodID(env, s_cls, "select",   "(Landroid/view/View;I)V");
    s_selected = (*env)->GetStaticMethodID(env, s_cls, "selected", "(Landroid/view/View;)I");
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return false; }
    return s_create && s_addTab && s_select && s_selected;
}

Mel_Gui_Handle mel_tabview_create_opt(Mel_Gui_Handle parent, Mel_TabView_Opt o)
{
    Mel_Layout* layout = (Mel_Layout*)mel_calloc(mel_gui__alloc(), sizeof *layout);
    if (layout) layout->vtable = &s_tab_vtable;

    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    JNIEnv* env = mel_gui__android_env();
    if (!env) return h;

    jobject view = (*env)->CallStaticObjectMethod(env, s_cls, s_create,
        mel_gui__android_pack(h), (jlong)(intptr_t)o.on_select);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return h; }
    if (!view) return h;

    mel_gui__android_attach(n, view);
    mel_gui__android_install_focus(env, view, h, o.focus);
    (*env)->DeleteLocalRef(env, view);
    return h;
}

Mel_Gui_Handle mel_tab_create_opt(Mel_Gui_Handle tabview, Mel_Tab_Opt o)
{
    Mel_Gui_Node* tn = mel_gui__node(tabview);
    if (!tn || !tn->native) return MEL_GUI_HANDLE_NONE;

    Mel_Gui_Handle h = mel_gui__node_new(tabview, 0, 0, tn->width, tn->height - TABSTRIP_DP,
                                         o.id, o.user, false, &o.layoutable, o.layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    JNIEnv* env = mel_gui__android_env();
    if (!env) return h;

    jstring s    = mel_gui__android_jstring(env, o.title);
    jobject page = (*env)->CallStaticObjectMethod(env, s_cls, s_addTab, (jobject)tn->native, s);
    (*env)->DeleteLocalRef(env, s);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return h; }
    if (!page) return h;

    n->native = (*env)->NewGlobalRef(env, page);
    (*env)->DeleteLocalRef(env, page);
    return h;
}

void mel_tabview_select(Mel_Gui_Handle tabview, i32 index)
{
    Mel_Gui_Node* n = mel_gui__node(tabview);
    if (!n || !n->native) return;
    JNIEnv* env = mel_gui__android_env();
    if (env) (*env)->CallStaticVoidMethod(env, s_cls, s_select, (jobject)n->native, (jint)index);
}

i32 mel_tabview_selected(Mel_Gui_Handle tabview)
{
    Mel_Gui_Node* n = mel_gui__node(tabview);
    if (!n || !n->native) return -1;
    JNIEnv* env = mel_gui__android_env();
    if (!env) return -1;
    return (i32)(*env)->CallStaticIntMethod(env, s_cls, s_selected, (jobject)n->native);
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelTabView_nativeSelect(JNIEnv* env, jclass cls, jlong h, jlong fn, jint index)
{
    (void)env; (void)cls;
    if (!fn) return;
    Mel_Gui_Handle handle = mel_gui__android_unpack(h);
    ((Mel_Cb_I32)(intptr_t)fn)(handle, (i32)index, mel_gui_user(handle));
}
