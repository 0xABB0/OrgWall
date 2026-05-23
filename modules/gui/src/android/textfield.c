#include "android.h"

#include <stdint.h>
#include <string.h>

static jclass    s_cls;
static jmethodID s_create;  /* (Ljava/lang/String;J)Landroid/view/View; */

bool mel_gui__android_textfield_register_jni(JNIEnv* env)
{
    jclass cls = (*env)->FindClass(env, "orgwall/melody/platform/MelTextField");
    if (!cls) return false;
    s_cls = (jclass)(*env)->NewGlobalRef(env, cls);
    (*env)->DeleteLocalRef(env, cls);

    s_create = (*env)->GetStaticMethodID(env, s_cls, "create", "(JLjava/lang/String;J)Landroid/view/View;");
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return false; }
    return s_create != NULL;
}

Mel_Gui_Handle mel_textfield_create_opt(Mel_Gui_Handle parent, Mel_TextField_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    JNIEnv* env = mel_gui__android_env();
    if (!env) return h;

    jstring s = mel_gui__android_jstring(env, o.text);
    jobject view = (*env)->CallStaticObjectMethod(env, s_cls, s_create,
        mel_gui__android_pack(h), s, (jlong)(intptr_t)o.on_.on_text_changed);
    (*env)->DeleteLocalRef(env, s);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return h; }
    if (!view) return h;

    mel_gui__android_attach(n, view);
    mel_gui__android_install_focus(env, view, h, o.focus);

    (*env)->DeleteLocalRef(env, view);
    return h;
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelTextField_nativeTextChanged(JNIEnv* env, jclass cls, jlong h, jlong fn, jstring text)
{
    (void)cls;
    if (!fn) return;
    Mel_Gui_Handle handle = mel_gui__android_unpack(h);

    const char* c = text ? (*env)->GetStringUTFChars(env, text, NULL) : NULL;
    str8 s = { (u8*)c, c ? (size)strlen(c) : 0 };
    ((Mel_Cb_Str8)(intptr_t)fn)(handle, s, mel_gui_user(handle));
    if (c) (*env)->ReleaseStringUTFChars(env, text, c);
}
