#include "android.h"

#include <stdint.h>

static jclass    s_cls;
static jmethodID s_create;         /* (III)Landroid/view/View; */
static jmethodID s_installChange;  /* (Landroid/view/View;JJ)V */
static jmethodID s_value;          /* MelSeekBar.melValue ()I */
static jmethodID s_setValue;       /* MelSeekBar.melSetValue (I)V */

bool mel_gui__android_slider_register_jni(JNIEnv* env)
{
    jclass cls = (*env)->FindClass(env, "orgwall/melody/platform/MelSlider");
    jclass sb  = (*env)->FindClass(env, "orgwall/melody/platform/MelSeekBar");
    if (!cls || !sb) return false;
    s_cls = (jclass)(*env)->NewGlobalRef(env, cls);
    (*env)->DeleteLocalRef(env, cls);

    s_create        = (*env)->GetStaticMethodID(env, s_cls, "create",        "(III)Landroid/view/View;");
    s_installChange = (*env)->GetStaticMethodID(env, s_cls, "installChange", "(Landroid/view/View;JJ)V");
    s_value         = (*env)->GetMethodID(env, sb, "melValue",    "()I");
    s_setValue      = (*env)->GetMethodID(env, sb, "melSetValue", "(I)V");
    (*env)->DeleteLocalRef(env, sb);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return false; }
    return s_create && s_installChange && s_value && s_setValue;
}

Mel_Gui_Handle mel_slider_create_opt(Mel_Gui_Handle parent, Mel_Slider_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    i32 max_value = (o.max_value > o.min_value) ? o.max_value : o.min_value + 100;

    JNIEnv* env = mel_gui__android_env();
    if (!env) return h;

    jobject view = (*env)->CallStaticObjectMethod(env, s_cls, s_create, o.min_value, max_value, o.value);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return h; }
    if (!view) return h;

    mel_gui__android_attach(n, view);

    if (o.on_.on_value_changed) {
        (*env)->CallStaticVoidMethod(env, s_cls, s_installChange, view,
            mel_gui__android_pack(h), (jlong)(intptr_t)o.on_.on_value_changed);
    }
    mel_gui__android_install_focus(env, view, h, o.focus);

    (*env)->DeleteLocalRef(env, view);
    return h;
}

i32 mel_slider_value(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native) return 0;
    JNIEnv* env = mel_gui__android_env();
    if (!env) return 0;
    return (i32)(*env)->CallIntMethod(env, (jobject)n->native, s_value);
}

void mel_slider_set_value(Mel_Gui_Handle h, i32 value)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native) return;
    JNIEnv* env = mel_gui__android_env();
    if (!env) return;
    (*env)->CallVoidMethod(env, (jobject)n->native, s_setValue, value);
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelSlider_nativeChange(JNIEnv* env, jclass cls, jlong h, jlong fn, jint value)
{
    (void)env; (void)cls;
    if (!fn) return;
    Mel_Gui_Handle handle = mel_gui__android_unpack(h);
    ((Mel_Cb_I32)(intptr_t)fn)(handle, (i32)value, mel_gui_user(handle));
}
