#include <gui.control/window.h>
#include <gui.platform.android/gui.platform.android.h>

static jclass    mel__a_window_class;
static jmethodID mel__a_window_ctor;

static void mel__a_window_ensure(JNIEnv* env)
{
    if (mel__a_window_class != NULL) return;
    jclass local = (*env)->FindClass(env, "android/widget/FrameLayout");
    mel__a_window_class = (*env)->NewGlobalRef(env, local);
    (*env)->DeleteLocalRef(env, local);
    mel__a_window_ctor  = (*env)->GetMethodID(env, mel__a_window_class, "<init>", "(Landroid/content/Context;)V");
}

static jobject mel__window_construct(JNIEnv* env, jobject host, jclass host_class, Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc)
{
    MEL_UNUSED(host);
    MEL_UNUSED(host_class);
    MEL_UNUSED(h);
    MEL_UNUSED(desc);
    mel__a_window_ensure(env);

    jobject activity = mel_gui_android_activity(env);
    jobject view = (*env)->NewObject(env, mel__a_window_class, mel__a_window_ctor, activity);
    (*env)->DeleteLocalRef(env, activity);
    return view;
}

void mel_gui_window_platform_register(Mel_Atom atom)
{
    mel_gui_android_register_constructor(atom, mel__window_construct);
}
