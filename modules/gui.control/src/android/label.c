#include <gui.control/label.h>
#include <gui.platform.android/gui.platform.android.h>

#define MEL_A_FG_COLOR    0xFFF5F1E8u
#define MEL_A_GRAVITY_CV  0x10

static jclass    mel__a_label_class;
static jmethodID mel__a_label_ctor;
static jmethodID mel__a_label_set_text;
static jmethodID mel__a_label_set_text_color;
static jmethodID mel__a_label_set_text_size;
static jmethodID mel__a_label_set_gravity;

static void mel__a_label_ensure(JNIEnv* env)
{
    if (mel__a_label_class != NULL) return;
    jclass local = (*env)->FindClass(env, "android/widget/TextView");
    mel__a_label_class          = (*env)->NewGlobalRef(env, local);
    (*env)->DeleteLocalRef(env, local);
    mel__a_label_ctor           = (*env)->GetMethodID(env, mel__a_label_class, "<init>",       "(Landroid/content/Context;)V");
    mel__a_label_set_text       = (*env)->GetMethodID(env, mel__a_label_class, "setText",      "(Ljava/lang/CharSequence;)V");
    mel__a_label_set_text_color = (*env)->GetMethodID(env, mel__a_label_class, "setTextColor", "(I)V");
    mel__a_label_set_text_size  = (*env)->GetMethodID(env, mel__a_label_class, "setTextSize",  "(F)V");
    mel__a_label_set_gravity    = (*env)->GetMethodID(env, mel__a_label_class, "setGravity",   "(I)V");
}

static jobject mel__label_construct(JNIEnv* env, jobject host, jclass host_class, Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc)
{
    MEL_UNUSED(host);
    MEL_UNUSED(host_class);
    MEL_UNUSED(h);
    mel__a_label_ensure(env);

    jobject activity = mel_gui_android_activity(env);
    jobject view = (*env)->NewObject(env, mel__a_label_class, mel__a_label_ctor, activity);
    (*env)->DeleteLocalRef(env, activity);

    jstring text = mel_gui_android_utf16(env, desc->text);
    (*env)->CallVoidMethod(env, view, mel__a_label_set_text, text);
    (*env)->DeleteLocalRef(env, text);

    (*env)->CallVoidMethod(env, view, mel__a_label_set_text_color, (jint)MEL_A_FG_COLOR);
    (*env)->CallVoidMethod(env, view, mel__a_label_set_text_size,  desc->id == 1u ? 26.0f : 15.0f);
    (*env)->CallVoidMethod(env, view, mel__a_label_set_gravity,    (jint)MEL_A_GRAVITY_CV);

    return view;
}

void mel_gui_label_platform_register(Mel_Atom atom)
{
    mel_gui_android_register_constructor(atom, mel__label_construct);
}
