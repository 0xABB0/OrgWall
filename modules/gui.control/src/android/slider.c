#include <gui.control/slider.h>
#include <gui.platform.android/gui.platform.android.h>

static jclass    mel__a_slider_class;
static jmethodID mel__a_slider_ctor;
static jmethodID mel__a_slider_set_max;
static jmethodID mel__a_slider_set_progress;
static jmethodID mel__a_slider_set_listener;

static void mel__a_slider_ensure(JNIEnv* env)
{
    if (mel__a_slider_class != NULL) return;
    jclass local = (*env)->FindClass(env, "android/widget/SeekBar");
    mel__a_slider_class        = (*env)->NewGlobalRef(env, local);
    (*env)->DeleteLocalRef(env, local);
    mel__a_slider_ctor         = (*env)->GetMethodID(env, mel__a_slider_class, "<init>",                     "(Landroid/content/Context;)V");
    mel__a_slider_set_max      = (*env)->GetMethodID(env, mel__a_slider_class, "setMax",                     "(I)V");
    mel__a_slider_set_progress = (*env)->GetMethodID(env, mel__a_slider_class, "setProgress",                "(I)V");
    mel__a_slider_set_listener = (*env)->GetMethodID(env, mel__a_slider_class, "setOnSeekBarChangeListener", "(Landroid/widget/SeekBar$OnSeekBarChangeListener;)V");
}

static jobject mel__slider_construct(JNIEnv* env, jobject host, jclass host_class, Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc)
{
    MEL_UNUSED(host);
    MEL_UNUSED(host_class);
    MEL_UNUSED(desc);
    mel__a_slider_ensure(env);

    jobject activity = mel_gui_android_activity(env);
    jobject view = (*env)->NewObject(env, mel__a_slider_class, mel__a_slider_ctor, activity);
    (*env)->DeleteLocalRef(env, activity);

    (*env)->CallVoidMethod(env, view, mel__a_slider_set_max, (jint)100);
    (*env)->CallVoidMethod(env, view, mel__a_slider_set_progress, (jint)65);

    jobject listener = mel_gui_android_new_listener(env, h);
    (*env)->CallVoidMethod(env, view, mel__a_slider_set_listener, listener);
    mel_gui_android_install_focus_listener(env, view, listener);
    (*env)->DeleteLocalRef(env, listener);

    return view;
}

void mel_gui_slider_platform_register(Mel_Atom atom)
{
    mel_gui_android_register_constructor(atom, mel__slider_construct);
}
