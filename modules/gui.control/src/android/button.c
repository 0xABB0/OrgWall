#include <gui.control/button.h>
#include <gui.platform.android/gui.platform.android.h>

static jclass    mel__a_button_class;
static jmethodID mel__a_button_ctor;
static jmethodID mel__a_button_set_text;
static jmethodID mel__a_button_set_all_caps;
static jmethodID mel__a_button_set_on_click;

static void mel__a_button_ensure(JNIEnv* env)
{
    if (mel__a_button_class != NULL) return;
    jclass local = (*env)->FindClass(env, "android/widget/Button");
    mel__a_button_class        = (*env)->NewGlobalRef(env, local);
    (*env)->DeleteLocalRef(env, local);
    mel__a_button_ctor         = (*env)->GetMethodID(env, mel__a_button_class, "<init>",             "(Landroid/content/Context;)V");
    mel__a_button_set_text     = (*env)->GetMethodID(env, mel__a_button_class, "setText",            "(Ljava/lang/CharSequence;)V");
    mel__a_button_set_all_caps = (*env)->GetMethodID(env, mel__a_button_class, "setAllCaps",         "(Z)V");
    mel__a_button_set_on_click = (*env)->GetMethodID(env, mel__a_button_class, "setOnClickListener", "(Landroid/view/View$OnClickListener;)V");
}

static jobject mel__button_construct(JNIEnv* env, jobject host, jclass host_class, Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc)
{
    MEL_UNUSED(host);
    MEL_UNUSED(host_class);
    mel__a_button_ensure(env);

    jobject activity = mel_gui_android_activity(env);
    jobject view = (*env)->NewObject(env, mel__a_button_class, mel__a_button_ctor, activity);
    (*env)->DeleteLocalRef(env, activity);

    jstring text = mel_gui_android_utf16(env, desc->text);
    (*env)->CallVoidMethod(env, view, mel__a_button_set_text, text);
    (*env)->DeleteLocalRef(env, text);

    (*env)->CallVoidMethod(env, view, mel__a_button_set_all_caps, JNI_FALSE);

    jobject listener = mel_gui_android_new_listener(env, h);
    (*env)->CallVoidMethod(env, view, mel__a_button_set_on_click, listener);
    mel_gui_android_install_focus_listener(env, view, listener);
    (*env)->DeleteLocalRef(env, listener);

    return view;
}

void mel_gui_button_platform_register(Mel_Atom atom)
{
    mel_gui_android_register_constructor(atom, mel__button_construct);
}
