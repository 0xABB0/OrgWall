#include "android.h"

#include <stdint.h>

static jclass    s_cls;
static jmethodID s_create;         /* (Ljava/lang/String;Z)Landroid/view/View; */
static jmethodID s_installToggle;  /* (Landroid/view/View;JJ)V */
static jmethodID s_isChecked;      /* CompoundButton.isChecked ()Z */

bool mel_gui__android_checkbox_register_jni(JNIEnv* env)
{
    jclass cls = (*env)->FindClass(env, "orgwall/melody/platform/MelCheckBox");
    jclass cb  = (*env)->FindClass(env, "android/widget/CompoundButton");
    if (!cls || !cb) return false;
    s_cls = (jclass)(*env)->NewGlobalRef(env, cls);
    (*env)->DeleteLocalRef(env, cls);

    s_create        = (*env)->GetStaticMethodID(env, s_cls, "create",        "(Ljava/lang/String;Z)Landroid/view/View;");
    s_installToggle = (*env)->GetStaticMethodID(env, s_cls, "installToggle", "(Landroid/view/View;JJ)V");
    s_isChecked     = (*env)->GetMethodID(env, cb, "isChecked", "()Z");
    (*env)->DeleteLocalRef(env, cb);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return false; }
    return s_create && s_installToggle && s_isChecked;
}

Mel_Gui_Handle mel_checkbox_create_opt(Mel_Gui_Handle parent, Mel_CheckBox_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    JNIEnv* env = mel_gui__android_env();
    if (!env) return h;

    jstring s = mel_gui__android_jstring(env, o.text);
    jobject view = (*env)->CallStaticObjectMethod(env, s_cls, s_create, s, (jboolean)o.checked);
    (*env)->DeleteLocalRef(env, s);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return h; }
    if (!view) return h;

    mel_gui__android_attach(n, view);

    if (o.on_.on_toggled) {
        (*env)->CallStaticVoidMethod(env, s_cls, s_installToggle, view,
            mel_gui__android_pack(h), (jlong)(intptr_t)o.on_.on_toggled);
    }
    mel_gui__android_install_focus(env, view, h, o.focus);

    (*env)->DeleteLocalRef(env, view);
    return h;
}

bool mel_checkbox_checked(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native) return false;
    JNIEnv* env = mel_gui__android_env();
    if (!env) return false;
    return (bool)(*env)->CallBooleanMethod(env, (jobject)n->native, s_isChecked);
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelCheckBox_nativeToggle(JNIEnv* env, jclass cls, jlong h, jlong fn, jboolean checked)
{
    (void)env; (void)cls;
    if (!fn) return;
    Mel_Gui_Handle handle = mel_gui__android_unpack(h);
    ((Mel_Cb_Bool)(intptr_t)fn)(handle, (bool)checked, mel_gui_user(handle));
}
