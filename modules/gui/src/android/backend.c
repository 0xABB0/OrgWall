#include "android.h"

#include <platform/android/jni.h>
#include <string.h>

static Mel_Gui_Android g_a;

Mel_Gui_Android* mel_gui__android(void) { return &g_a; }

JNIEnv* mel_gui__android_env(void) { return mel_platform_android_env(); }

jstring mel_gui__android_jstring(JNIEnv* env, str8 s)
{
    if (s.len <= 0 || s.data == NULL) return (*env)->NewStringUTF(env, "");

    char  tmp[1024];
    char* buf = tmp;
    size  n   = s.len;
    if (n >= (size)sizeof(tmp)) {
        buf = (char*)mel_alloc(mel_gui__alloc(), (usize)n + 1);
        if (!buf) return (*env)->NewStringUTF(env, "");
    }
    memcpy(buf, s.data, (usize)n);
    buf[n] = 0;
    jstring js = (*env)->NewStringUTF(env, buf);
    if (buf != tmp) mel_dealloc(mel_gui__alloc(), buf);
    return js;
}

jlong mel_gui__android_pack(Mel_Gui_Handle h)
{
    return (jlong)mel_gui_handle_pack(h);
}

Mel_Gui_Handle mel_gui__android_unpack(jlong p)
{
    return mel_gui_handle_unpack((u64)p);
}

bool mel_gui__android_register(JNIEnv* env, jclass cls)
{
    g_a.cls = (jclass)(*env)->NewGlobalRef(env, cls);

    g_a.destroyView      = (*env)->GetStaticMethodID(env, g_a.cls, "destroyView",      "(J)V");
    g_a.setText          = (*env)->GetStaticMethodID(env, g_a.cls, "setText",          "(JLjava/lang/String;)V");
    g_a.getText          = (*env)->GetStaticMethodID(env, g_a.cls, "getText",          "(J)Ljava/lang/String;");
    g_a.setBounds        = (*env)->GetStaticMethodID(env, g_a.cls, "setBounds",        "(JIIII)V");
    g_a.setVisible       = (*env)->GetStaticMethodID(env, g_a.cls, "setVisible",       "(JZ)V");
    g_a.setEnabled       = (*env)->GetStaticMethodID(env, g_a.cls, "setEnabled",       "(JZ)V");
    g_a.setFocus         = (*env)->GetStaticMethodID(env, g_a.cls, "setFocus",         "(J)V");
    g_a.invalidate       = (*env)->GetStaticMethodID(env, g_a.cls, "invalidate",       "(J)V");
    g_a.presentFrame     = (*env)->GetStaticMethodID(env, g_a.cls, "presentFrame",     "(JLjava/lang/String;)V");
    g_a.setActivityTitle = (*env)->GetStaticMethodID(env, g_a.cls, "setActivityTitle", "(Ljava/lang/String;)V");

    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return false; }

    bool ok = true;
    ok &= mel_gui__android_frame_register_jni    (env);
    ok &= mel_gui__android_label_register_jni    (env);
    ok &= mel_gui__android_button_register_jni   (env);
    ok &= mel_gui__android_checkbox_register_jni (env);
    ok &= mel_gui__android_textfield_register_jni(env);
    ok &= mel_gui__android_slider_register_jni   (env);
    ok &= mel_gui__android_canvas_register_jni   (env);
    return ok;
}

bool mel_gui__backend_init(void)
{
    return g_a.cls != NULL;
}

void mel_gui__backend_destroy(Mel_Gui_Widget* w)
{
    if (!w || !w->native) return;
    bool toplevel = mel_gui__is_toplevel(w);

    JNIEnv* env = mel_gui__android_env();
    if (env) {
        (*env)->CallStaticVoidMethod(env, g_a.cls, g_a.destroyView, mel_gui__android_pack(w->self));
        (*env)->DeleteGlobalRef(env, (jobject)w->native);
    }
    w->native = NULL;

    if (toplevel && mel_gui__frames_dec() == 0) {
        Mel_Reactor* r = mel_gui__reactor();
        if (r) mel_reactor_quit(r);
    }
}

void mel_gui__backend_set_text(Mel_Gui_Widget* w, str8 text)
{
    if (!w || !w->native) return;

    if (mel_gui__is_toplevel(w)) {
        Mel_Gui_Frame_Impl* fi = (Mel_Gui_Frame_Impl*)w->impl;
        if (fi) {
            size n = text.len < (size)sizeof(fi->title) - 1 ? text.len : (size)sizeof(fi->title) - 1;
            if (n > 0 && text.data) memcpy(fi->title, text.data, (usize)n);
            fi->title[n] = 0;
        }
        if (!w->hidden) {
            JNIEnv* env = mel_gui__android_env();
            if (env) {
                jstring s = mel_gui__android_jstring(env, text);
                (*env)->CallStaticVoidMethod(env, g_a.cls, g_a.setActivityTitle, s);
                (*env)->DeleteLocalRef(env, s);
            }
        }
        return;
    }

    JNIEnv* env = mel_gui__android_env();
    if (!env) return;
    jstring s = mel_gui__android_jstring(env, text);
    (*env)->CallStaticVoidMethod(env, g_a.cls, g_a.setText, mel_gui__android_pack(w->self), s);
    (*env)->DeleteLocalRef(env, s);
}

size mel_gui__backend_get_text(Mel_Gui_Widget* w, char* buf, size cap)
{
    if (buf && cap > 0) buf[0] = 0;
    if (!w || !w->native || !buf || cap <= 0) return 0;

    if (mel_gui__is_toplevel(w)) {
        Mel_Gui_Frame_Impl* fi = (Mel_Gui_Frame_Impl*)w->impl;
        if (!fi) return 0;
        size n = (size)strlen(fi->title);
        if (n > cap - 1) n = cap - 1;
        memcpy(buf, fi->title, (usize)n);
        buf[n] = 0;
        return n;
    }

    JNIEnv* env = mel_gui__android_env();
    if (!env) return 0;

    jstring s = (jstring)(*env)->CallStaticObjectMethod(env, g_a.cls, g_a.getText, mel_gui__android_pack(w->self));
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return 0; }
    if (!s) return 0;

    const char* c = (*env)->GetStringUTFChars(env, s, NULL);
    if (!c) { (*env)->DeleteLocalRef(env, s); return 0; }

    size n = (size)strlen(c);
    if (n > cap - 1) n = cap - 1;
    memcpy(buf, c, (usize)n);
    buf[n] = 0;

    (*env)->ReleaseStringUTFChars(env, s, c);
    (*env)->DeleteLocalRef(env, s);
    return n;
}

void mel_gui__backend_set_bounds(Mel_Gui_Widget* w, i32 x, i32 y, i32 width, i32 height)
{
    if (!w || !w->native) return;
    JNIEnv* env = mel_gui__android_env();
    if (!env) return;
    (*env)->CallStaticVoidMethod(env, g_a.cls, g_a.setBounds,
        mel_gui__android_pack(w->self), x, y, width, height);
}

void mel_gui__backend_set_visible(Mel_Gui_Widget* w, bool visible)
{
    if (!w || !w->native) return;
    JNIEnv* env = mel_gui__android_env();
    if (!env) return;

    if (mel_gui__is_toplevel(w) && visible) {
        Mel_Gui_Frame_Impl* fi = (Mel_Gui_Frame_Impl*)w->impl;
        jstring jt = fi ? mel_gui__android_jstring(env, (str8){ (u8*)fi->title, (size)strlen(fi->title) }) : NULL;
        (*env)->CallStaticVoidMethod(env, g_a.cls, g_a.presentFrame, mel_gui__android_pack(w->self), jt);
        if (jt) (*env)->DeleteLocalRef(env, jt);
    } else {
        (*env)->CallStaticVoidMethod(env, g_a.cls, g_a.setVisible,
            mel_gui__android_pack(w->self), (jboolean)visible);
    }
}

void mel_gui__backend_set_enabled(Mel_Gui_Widget* w, bool enabled)
{
    if (!w || !w->native) return;
    JNIEnv* env = mel_gui__android_env();
    if (!env) return;
    (*env)->CallStaticVoidMethod(env, g_a.cls, g_a.setEnabled,
        mel_gui__android_pack(w->self), (jboolean)enabled);
}

void mel_gui__backend_set_focus(Mel_Gui_Widget* w)
{
    if (!w || !w->native) return;
    if (mel_gui__is_toplevel(w)) return;

    JNIEnv* env = mel_gui__android_env();
    if (!env) return;
    (*env)->CallStaticVoidMethod(env, g_a.cls, g_a.setFocus, mel_gui__android_pack(w->self));
}

void mel_gui__backend_invalidate(Mel_Gui_Widget* w)
{
    if (!w || !w->native) return;
    JNIEnv* env = mel_gui__android_env();
    if (!env) return;
    (*env)->CallStaticVoidMethod(env, g_a.cls, g_a.invalidate, mel_gui__android_pack(w->self));
}
