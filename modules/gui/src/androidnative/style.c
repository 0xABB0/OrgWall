#include "android.h"

static jclass    s_cls;
static jmethodID s_apply; /* (Landroid/view/View;ZIZIZIFFFIZLjava/lang/String;IIII)V */

bool mel_gui__android_style_register_jni(JNIEnv* env)
{
    jclass cls = (*env)->FindClass(env, "orgwall/melody/platform/MelStyle");
    if (!cls)
        return false;
    s_cls = (jclass)(*env)->NewGlobalRef(env, cls);
    (*env)->DeleteLocalRef(env, cls);

    s_apply = (*env)->GetStaticMethodID(env, s_cls, "apply", "(Landroid/view/View;ZIZIZIFFFIZLjava/lang/String;IIII)V");
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return false;
    }
    return s_apply != NULL;
}

static jint argb(mel_color8 c) { return (jint)(((u32)c.a << 24) | ((u32)c.r << 16) | ((u32)c.g << 8) | (u32)c.b); }

void mel_gui_set_style(Mel_Gui_Handle h, Mel_Style style)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native)
        return;

    JNIEnv* env = mel_gui__android_env();
    if (!env)
        return;

    /* A border needs both a width and a color; style.h names width 0 as "no
     * border", and inventing a color would be a silent default. */
    bool has_border = style.border_color.set && style.border_width > 0;

    jstring family = mel_gui__android_jstring(env, style.font_family);
    (*env)->CallStaticVoidMethod(env,
                                 s_cls,
                                 s_apply,
                                 (jobject)n->native,
                                 (jboolean)style.fg.set,
                                 argb(style.fg.color),
                                 (jboolean)style.bg.set,
                                 argb(style.bg.color),
                                 (jboolean)has_border,
                                 argb(style.border_color.color),
                                 (jfloat)style.border_width,
                                 (jfloat)style.corner_radius,
                                 (jfloat)style.font_size,
                                 (jint)style.font_weight,
                                 (jboolean)style.italic,
                                 family,
                                 (jint)style.padding_l,
                                 (jint)style.padding_t,
                                 (jint)style.padding_r,
                                 (jint)style.padding_b);
    (*env)->DeleteLocalRef(env, family);
    if ((*env)->ExceptionCheck(env))
        (*env)->ExceptionClear(env);
}
