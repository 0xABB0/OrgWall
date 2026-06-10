#include "android.h"

#include <platform/android/jni.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#define MEL_ANDROID_VISIBLE          0
#define MEL_ANDROID_GONE             8

/* ViewGroup.LayoutParams + android.view.Gravity protocol values. */
#define MEL_ANDROID_MATCH_PARENT     (-1)
#define MEL_ANDROID_WRAP_CONTENT     (-2)
#define MEL_ANDROID_GRAVITY_LEFT     3
#define MEL_ANDROID_GRAVITY_RIGHT    5
#define MEL_ANDROID_GRAVITY_TOP      48
#define MEL_ANDROID_GRAVITY_BOTTOM   80
#define MEL_ANDROID_GRAVITY_CENTER_H 1
#define MEL_ANDROID_GRAVITY_CENTER_V 16

static Mel_Gui_Android g_a;

Mel_Gui_Android* mel_gui__android(void) { return &g_a; }

JNIEnv* mel_gui__android_env(void) { return mel_platform_android_env(); }

jstring mel_gui__android_jstring(JNIEnv* env, str8 s)
{
    if (s.len <= 0 || s.data == NULL)
        return (*env)->NewStringUTF(env, "");

    char  tmp[1024];
    char* buf = tmp;
    size  n = s.len;
    if (n >= (size)sizeof(tmp))
    {
        buf = (char*)mel_alloc(mel_gui__alloc(), (usize)n + 1);
        if (!buf)
            return (*env)->NewStringUTF(env, "");
    }
    memcpy(buf, s.data, (usize)n);
    buf[n] = 0;
    jstring js = (*env)->NewStringUTF(env, buf);
    if (buf != tmp)
        mel_dealloc(mel_gui__alloc(), buf);
    return js;
}

jlong          mel_gui__android_pack(Mel_Gui_Handle h) { return (jlong)mel_gui_handle_pack(h); }
Mel_Gui_Handle mel_gui__android_unpack(jlong p) { return mel_gui_handle_unpack((u64)p); }

int mel_gui__android_dp2px(i32 dp) { return (int)lroundf((float)dp * g_a.density); }

i32 mel_gui__android_px2dp(int px) { return (i32)lroundf((float)px / (g_a.density > 0 ? g_a.density : 1.0f)); }

static jobject node_view(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    return n ? (jobject)n->native : NULL;
}

static u8 resolve_cross_align(const Mel_Linear_Layout* lin, const Mel_Layoutable* s)
{
    u8 a = s->cross_align != MEL_ALIGN_DEFAULT ? s->cross_align : lin->cross_align;
    return a == MEL_ALIGN_DEFAULT ? MEL_ALIGN_START : a;
}

jobject mel_gui__android_linear_params(JNIEnv* env, const Mel_Linear_Layout* lin, const Mel_Gui_Node* child)
{
    const Mel_Layoutable* s = &child->layoutable;
    const bool            v = lin->vertical;

    i32 pref_main = v ? (s->fixed_h ? s->fixed_h : s->preferred_h) : (s->fixed_w ? s->fixed_w : s->preferred_w);
    i32 pref_cross = v ? (s->fixed_w ? s->fixed_w : s->preferred_w) : (s->fixed_h ? s->fixed_h : s->preferred_h);
    u8  align = resolve_cross_align(lin, s);

    jint main_px = pref_main ? (jint)mel_gui__android_dp2px(pref_main) : (s->weight > 0 ? 0 : MEL_ANDROID_WRAP_CONTENT);
    jint cross_px = pref_cross ? (jint)mel_gui__android_dp2px(pref_cross) : (align == MEL_ALIGN_STRETCH ? MEL_ANDROID_MATCH_PARENT : MEL_ANDROID_WRAP_CONTENT);

    jobject lp = (*env)->NewObject(env, g_a.llp_cls, g_a.llp_ctor, v ? cross_px : main_px, v ? main_px : cross_px, (jfloat)s->weight);
    if (!lp)
        return NULL;

    jint gravity = -1;
    if (align == MEL_ALIGN_START)
        gravity = v ? MEL_ANDROID_GRAVITY_LEFT : MEL_ANDROID_GRAVITY_TOP;
    else if (align == MEL_ALIGN_CENTER)
        gravity = v ? MEL_ANDROID_GRAVITY_CENTER_H : MEL_ANDROID_GRAVITY_CENTER_V;
    else if (align == MEL_ALIGN_END)
        gravity = v ? MEL_ANDROID_GRAVITY_RIGHT : MEL_ANDROID_GRAVITY_BOTTOM;
    if (gravity != -1)
        (*env)->SetIntField(env, lp, g_a.llp_gravity, gravity);

    (*env)->SetIntField(env, lp, g_a.llp_leftMargin, mel_gui__android_dp2px(s->margin_l));
    (*env)->SetIntField(env, lp, g_a.llp_topMargin, mel_gui__android_dp2px(s->margin_t));
    (*env)->SetIntField(env, lp, g_a.llp_rightMargin, mel_gui__android_dp2px(s->margin_r));
    (*env)->SetIntField(env, lp, g_a.llp_bottomMargin, mel_gui__android_dp2px(s->margin_b));
    return lp;
}

static bool parent_is_linear(const Mel_Gui_Node* parent) { return parent && parent->lowered && parent->layout && parent->layout->cls == mel_linear_layout_class(); }

void mel_gui__android_attach(Mel_Gui_Node* n, jobject view)
{
    JNIEnv* env = mel_gui__android_env();
    if (!env || !view)
        return;

    Mel_Gui_Node* parent = mel_gui__node(n->parent);
    jobject       pview = parent ? (jobject)(parent->content ? parent->content : parent->native) : NULL;
    if (pview)
    {
        jobject lp = NULL;
        if (parent_is_linear(parent))
            lp = mel_gui__android_linear_params(env, (const Mel_Linear_Layout*)parent->layout, n);
        if (!lp)
        {
            lp = (*env)->NewObject(env, g_a.lp_cls, g_a.lp_ctor, mel_gui__android_dp2px(n->width), mel_gui__android_dp2px(n->height));
            (*env)->SetIntField(env, lp, g_a.lp_leftMargin, mel_gui__android_dp2px(n->x));
            (*env)->SetIntField(env, lp, g_a.lp_topMargin, mel_gui__android_dp2px(n->y));
        }
        (*env)->CallVoidMethod(env, view, g_a.view_setLayoutParams, lp);
        (*env)->DeleteLocalRef(env, lp);
        (*env)->CallVoidMethod(env, pview, g_a.vg_addView, view);
    }

    n->native = (*env)->NewGlobalRef(env, view);
}

void mel_gui__android_install_focus(JNIEnv* env, jobject view, Mel_Gui_Handle h, Mel_Gui_Focus_Cb focus)
{
    if (!focus.on_focus_in && !focus.on_focus_out)
        return;
    (*env)->CallStaticVoidMethod(env, g_a.mel_cls, g_a.mel_installFocus, view, mel_gui__android_pack(h), (jlong)(intptr_t)focus.on_focus_in, (jlong)(intptr_t)focus.on_focus_out);
}

static bool cache_framework(JNIEnv* env)
{
    jclass view = (*env)->FindClass(env, "android/view/View");
    jclass vg = (*env)->FindClass(env, "android/view/ViewGroup");
    jclass lp = (*env)->FindClass(env, "android/widget/FrameLayout$LayoutParams");
    jclass llp = (*env)->FindClass(env, "android/widget/LinearLayout$LayoutParams");
    jclass tv = (*env)->FindClass(env, "android/widget/TextView");
    jclass obj = (*env)->FindClass(env, "java/lang/Object");
    if (!view || !vg || !lp || !llp || !tv || !obj)
        return false;

    g_a.view_setVisibility = (*env)->GetMethodID(env, view, "setVisibility", "(I)V");
    g_a.view_setEnabled = (*env)->GetMethodID(env, view, "setEnabled", "(Z)V");
    g_a.view_requestFocus = (*env)->GetMethodID(env, view, "requestFocus", "()Z");
    g_a.view_invalidate = (*env)->GetMethodID(env, view, "invalidate", "()V");
    g_a.view_setLayoutParams = (*env)->GetMethodID(env, view, "setLayoutParams", "(Landroid/view/ViewGroup$LayoutParams;)V");
    g_a.view_getParent = (*env)->GetMethodID(env, view, "getParent", "()Landroid/view/ViewParent;");
    g_a.view_setTag = (*env)->GetMethodID(env, view, "setTag", "(Ljava/lang/Object;)V");
    g_a.view_getTag = (*env)->GetMethodID(env, view, "getTag", "()Ljava/lang/Object;");
    g_a.view_measure = (*env)->GetMethodID(env, view, "measure", "(II)V");
    g_a.view_getMeasuredWidth = (*env)->GetMethodID(env, view, "getMeasuredWidth", "()I");
    g_a.view_getMeasuredHeight = (*env)->GetMethodID(env, view, "getMeasuredHeight", "()I");

    g_a.vg_cls = (jclass)(*env)->NewGlobalRef(env, vg);
    g_a.vg_addView = (*env)->GetMethodID(env, vg, "addView", "(Landroid/view/View;)V");
    g_a.vg_removeView = (*env)->GetMethodID(env, vg, "removeView", "(Landroid/view/View;)V");

    g_a.lp_cls = (jclass)(*env)->NewGlobalRef(env, lp);
    g_a.lp_ctor = (*env)->GetMethodID(env, lp, "<init>", "(II)V");
    g_a.lp_leftMargin = (*env)->GetFieldID(env, lp, "leftMargin", "I");
    g_a.lp_topMargin = (*env)->GetFieldID(env, lp, "topMargin", "I");

    g_a.llp_cls = (jclass)(*env)->NewGlobalRef(env, llp);
    g_a.llp_ctor = (*env)->GetMethodID(env, llp, "<init>", "(IIF)V");
    g_a.llp_gravity = (*env)->GetFieldID(env, llp, "gravity", "I");
    g_a.llp_leftMargin = (*env)->GetFieldID(env, llp, "leftMargin", "I");
    g_a.llp_topMargin = (*env)->GetFieldID(env, llp, "topMargin", "I");
    g_a.llp_rightMargin = (*env)->GetFieldID(env, llp, "rightMargin", "I");
    g_a.llp_bottomMargin = (*env)->GetFieldID(env, llp, "bottomMargin", "I");

    g_a.tv_cls = (jclass)(*env)->NewGlobalRef(env, tv);
    g_a.tv_setText = (*env)->GetMethodID(env, tv, "setText", "(Ljava/lang/CharSequence;)V");
    g_a.tv_getText = (*env)->GetMethodID(env, tv, "getText", "()Ljava/lang/CharSequence;");

    g_a.obj_toString = (*env)->GetMethodID(env, obj, "toString", "()Ljava/lang/String;");

    (*env)->DeleteLocalRef(env, view);
    (*env)->DeleteLocalRef(env, vg);
    (*env)->DeleteLocalRef(env, lp);
    (*env)->DeleteLocalRef(env, llp);
    (*env)->DeleteLocalRef(env, tv);
    (*env)->DeleteLocalRef(env, obj);

    return !(*env)->ExceptionCheck(env);
}

bool mel_gui__android_register(JNIEnv* env, jclass cls)
{
    g_a.mel_cls = (jclass)(*env)->NewGlobalRef(env, cls);
    g_a.mel_presentFrame = (*env)->GetStaticMethodID(env, g_a.mel_cls, "presentFrame", "(Landroid/view/View;)V");
    g_a.mel_back = (*env)->GetStaticMethodID(env, g_a.mel_cls, "back", "()Z");
    g_a.mel_setActivityTitle = (*env)->GetStaticMethodID(env, g_a.mel_cls, "setActivityTitle", "(Ljava/lang/String;)V");
    g_a.mel_installFocus = (*env)->GetStaticMethodID(env, g_a.mel_cls, "installFocus", "(Landroid/view/View;JJJ)V");

    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return false;
    }
    if (!cache_framework(env))
    {
        (*env)->ExceptionClear(env);
        return false;
    }

    bool ok = true;
    ok &= mel_gui__android_layout_register_jni(env);
    ok &= mel_gui__android_style_register_jni(env);
    ok &= mel_gui__android_frame_register_jni(env);
    ok &= mel_gui__android_dialog_register_jni(env);
    ok &= mel_gui__android_panel_register_jni(env);
    ok &= mel_gui__android_groupbox_register_jni(env);
    ok &= mel_gui__android_scrollview_register_jni(env);
    ok &= mel_gui__android_tabview_register_jni(env);
    ok &= mel_gui__android_splitter_register_jni(env);
    ok &= mel_gui__android_label_register_jni(env);
    ok &= mel_gui__android_button_register_jni(env);
    ok &= mel_gui__android_checkbox_register_jni(env);
    ok &= mel_gui__android_textfield_register_jni(env);
    ok &= mel_gui__android_slider_register_jni(env);
    ok &= mel_gui__android_canvas_register_jni(env);
    ok &= mel_gui__android_gpu_view_register_jni(env);
    return ok;
}

void mel_gui__android_set_density(float density) { g_a.density = density > 0 ? density : 1.0f; }

bool mel_gui__backend_init(void) { return g_a.mel_cls != NULL; }

void mel_gui__backend_destroy(Mel_Gui_Node* n)
{
    if (!n || !n->native)
        return;
    bool toplevel = mel_gui__is_toplevel(n);

    JNIEnv* env = mel_gui__android_env();
    if (env)
    {
        jobject view = (jobject)n->native;
        jobject parent = (*env)->CallObjectMethod(env, view, g_a.view_getParent);
        if (parent)
        {
            if ((*env)->IsInstanceOf(env, parent, g_a.vg_cls))
            {
                (*env)->CallVoidMethod(env, parent, g_a.vg_removeView, view);
            }
            (*env)->DeleteLocalRef(env, parent);
        }
        (*env)->DeleteGlobalRef(env, view);
        if (n->content)
            (*env)->DeleteGlobalRef(env, (jobject)n->content);
    }
    n->native = NULL;
    n->content = NULL;

    if (toplevel && mel_gui__frames_dec() == 0)
    {
        Mel_Vat* v = mel_gui__vat();
        if (v)
            mel_vat_quit(v);
    }
}

void mel_gui_set_text(Mel_Gui_Handle h, str8 text)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n)
        return;
    JNIEnv* env = mel_gui__android_env();
    if (!env)
        return;

    if (n->is_screen)
    {
        n->screen_title = text;
        Mel_Gui_Node* top = mel_gui__node(mel_gui__toplevel(h));
        if (top && top->native)
        {
            jstring s = mel_gui__android_jstring(env, text);
            (*env)->CallVoidMethod(env, (jobject)top->native, g_a.view_setTag, s);
            if (!top->hidden)
                (*env)->CallStaticVoidMethod(env, g_a.mel_cls, g_a.mel_setActivityTitle, s);
            (*env)->DeleteLocalRef(env, s);
        }
        return;
    }

    if (!n->native)
        return;

    jobject view = (jobject)n->native;
    jstring s = mel_gui__android_jstring(env, text);

    if (mel_gui__is_toplevel(n))
    {
        (*env)->CallVoidMethod(env, view, g_a.view_setTag, s);
        if (!n->hidden)
        {
            (*env)->CallStaticVoidMethod(env, g_a.mel_cls, g_a.mel_setActivityTitle, s);
        }
    }
    else
    {
        (*env)->CallVoidMethod(env, view, g_a.tv_setText, s);
    }
    (*env)->DeleteLocalRef(env, s);
}

size mel_gui_get_text(Mel_Gui_Handle h, char* buf, size cap)
{
    if (buf && cap > 0)
        buf[0] = 0;
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native || !buf || cap <= 0)
        return 0;
    JNIEnv* env = mel_gui__android_env();
    if (!env)
        return 0;

    if (n->is_screen)
        return str8_to_buf(n->screen_title, buf, cap);

    jobject view = (jobject)n->native;
    jobject cs = mel_gui__is_toplevel(n) ? (*env)->CallObjectMethod(env, view, g_a.view_getTag) : (*env)->CallObjectMethod(env, view, g_a.tv_getText);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return 0;
    }
    if (!cs)
        return 0;

    jstring s = (jstring)(*env)->CallObjectMethod(env, cs, g_a.obj_toString);
    (*env)->DeleteLocalRef(env, cs);
    if (!s)
        return 0;

    const char* c = (*env)->GetStringUTFChars(env, s, NULL);
    size        n2 = 0;
    if (c)
    {
        n2 = (size)strlen(c);
        if (n2 > cap - 1)
            n2 = cap - 1;
        memcpy(buf, c, (usize)n2);
        buf[n2] = 0;
        (*env)->ReleaseStringUTFChars(env, s, c);
    }
    (*env)->DeleteLocalRef(env, s);
    return n2;
}

void mel_gui_set_bounds(Mel_Gui_Handle h, i32 x, i32 y, i32 width, i32 height)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n)
        return;
    n->x = x;
    n->y = y;
    n->width = width;
    n->height = height;
    if (!n->native || mel_gui__is_toplevel(n))
        return;

    /* Under a lowered parent the native engine owns position/size; the x/y/w/h
     * mirror above is not authoritative there. */
    Mel_Gui_Node* parent = mel_gui__node(n->parent);
    if (parent && parent->lowered)
        return;

    JNIEnv* env = mel_gui__android_env();
    if (!env)
        return;
    jobject view = (jobject)n->native;

    jobject lp = (*env)->NewObject(env, g_a.lp_cls, g_a.lp_ctor, mel_gui__android_dp2px(width), mel_gui__android_dp2px(height));
    (*env)->SetIntField(env, lp, g_a.lp_leftMargin, mel_gui__android_dp2px(x));
    (*env)->SetIntField(env, lp, g_a.lp_topMargin, mel_gui__android_dp2px(y));
    (*env)->CallVoidMethod(env, view, g_a.view_setLayoutParams, lp);
    (*env)->DeleteLocalRef(env, lp);
}

void mel_gui_set_visible(Mel_Gui_Handle h, bool visible)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n)
        return;
    n->hidden = !visible;
    if (!n->native)
        return;
    JNIEnv* env = mel_gui__android_env();
    if (!env)
        return;
    jobject view = (jobject)n->native;

    if (mel_gui__is_toplevel(n) && visible)
    {
        (*env)->CallStaticVoidMethod(env, g_a.mel_cls, g_a.mel_presentFrame, view);
    }
    else
    {
        (*env)->CallVoidMethod(env, view, g_a.view_setVisibility, visible ? MEL_ANDROID_VISIBLE : MEL_ANDROID_GONE);
    }
}

void mel_gui__nav_replace(Mel_Gui_Handle next, Mel_Gui_Handle prev)
{
    (void)prev;
    mel_gui_set_visible(next, true);
}

void mel_gui__nav_back(Mel_Gui_Handle prev, Mel_Gui_Handle cur)
{
    (void)prev;
    (void)cur;
    JNIEnv* env = mel_gui__android_env();
    if (env)
        (*env)->CallStaticBooleanMethod(env, g_a.mel_cls, g_a.mel_back);
}

bool mel_gui_supports_multi_root(void) { return false; }

void mel_gui_set_enabled(Mel_Gui_Handle h, bool enabled)
{
    jobject view = node_view(h);
    if (!view)
        return;
    JNIEnv* env = mel_gui__android_env();
    if (env)
        (*env)->CallVoidMethod(env, view, g_a.view_setEnabled, (jboolean)enabled);
}

void mel_gui_set_focus(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native || mel_gui__is_toplevel(n))
        return;
    JNIEnv* env = mel_gui__android_env();
    if (env)
        (*env)->CallBooleanMethod(env, (jobject)n->native, g_a.view_requestFocus);
}

void mel_gui_invalidate(Mel_Gui_Handle h)
{
    jobject view = node_view(h);
    if (!view)
        return;
    JNIEnv* env = mel_gui__android_env();
    if (env)
        (*env)->CallVoidMethod(env, view, g_a.view_invalidate);
}
