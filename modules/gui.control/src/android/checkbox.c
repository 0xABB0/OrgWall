#include <gui.control/checkbox.h>
#include <gui.platform.android/gui.platform.android.h>

#define MEL_A_FG_COLOR 0xFFF5F1E8u

static jclass    mel__a_checkbox_class;
static jmethodID mel__a_checkbox_ctor;
static jmethodID mel__a_checkbox_set_text;
static jmethodID mel__a_checkbox_set_text_color;
static jmethodID mel__a_checkbox_set_text_size;
static jmethodID mel__a_checkbox_set_on_checked;

static void mel__a_checkbox_ensure(JNIEnv* env)
{
    if (mel__a_checkbox_class != NULL) return;
    jclass local = (*env)->FindClass(env, "android/widget/CheckBox");
    mel__a_checkbox_class          = (*env)->NewGlobalRef(env, local);
    (*env)->DeleteLocalRef(env, local);
    mel__a_checkbox_ctor           = (*env)->GetMethodID(env, mel__a_checkbox_class, "<init>",                     "(Landroid/content/Context;)V");
    mel__a_checkbox_set_text       = (*env)->GetMethodID(env, mel__a_checkbox_class, "setText",                    "(Ljava/lang/CharSequence;)V");
    mel__a_checkbox_set_text_color = (*env)->GetMethodID(env, mel__a_checkbox_class, "setTextColor",               "(I)V");
    mel__a_checkbox_set_text_size  = (*env)->GetMethodID(env, mel__a_checkbox_class, "setTextSize",                "(F)V");
    mel__a_checkbox_set_on_checked = (*env)->GetMethodID(env, mel__a_checkbox_class, "setOnCheckedChangeListener", "(Landroid/widget/CompoundButton$OnCheckedChangeListener;)V");
}

static jobject mel__checkbox_construct(JNIEnv* env, jobject host, jclass host_class, Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc)
{
    MEL_UNUSED(host);
    MEL_UNUSED(host_class);
    mel__a_checkbox_ensure(env);

    jobject activity = mel_gui_android_activity(env);
    jobject view = (*env)->NewObject(env, mel__a_checkbox_class, mel__a_checkbox_ctor, activity);
    (*env)->DeleteLocalRef(env, activity);

    jstring text = mel_gui_android_utf16(env, desc->text);
    (*env)->CallVoidMethod(env, view, mel__a_checkbox_set_text, text);
    (*env)->DeleteLocalRef(env, text);

    (*env)->CallVoidMethod(env, view, mel__a_checkbox_set_text_color, (jint)MEL_A_FG_COLOR);
    (*env)->CallVoidMethod(env, view, mel__a_checkbox_set_text_size,  15.0f);

    jobject listener = mel_gui_android_new_listener(env, h);
    (*env)->CallVoidMethod(env, view, mel__a_checkbox_set_on_checked, listener);
    mel_gui_android_install_focus_listener(env, view, listener);
    (*env)->DeleteLocalRef(env, listener);

    return view;
}

void mel_gui_checkbox_platform_register(Mel_Atom atom)
{
    mel_gui_android_register_constructor(atom, mel__checkbox_construct);
}
