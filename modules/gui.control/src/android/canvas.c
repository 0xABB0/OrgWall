#include <gui.control/canvas.h>
#include <gui.platform.android/gui.platform.android.h>

static jclass    mel__a_canvas_class;
static jmethodID mel__a_canvas_ctor;

static void mel__a_canvas_ensure(JNIEnv* env)
{
    if (mel__a_canvas_class != NULL) return;
    jclass local = (*env)->FindClass(env, "orgwall/melody/platform/MelCanvasView");
    mel__a_canvas_class = (*env)->NewGlobalRef(env, local);
    (*env)->DeleteLocalRef(env, local);
    mel__a_canvas_ctor  = (*env)->GetMethodID(env, mel__a_canvas_class, "<init>", "(Landroid/content/Context;J)V");
}

static jobject mel__canvas_construct(JNIEnv* env, jobject host, jclass host_class, Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc)
{
    MEL_UNUSED(host);
    MEL_UNUSED(host_class);
    MEL_UNUSED(desc);
    mel__a_canvas_ensure(env);

    jobject activity = mel_gui_android_activity(env);
    jobject view = (*env)->NewObject(env, mel__a_canvas_class, mel__a_canvas_ctor, activity, mel_gui_android_pack_handle(h));
    (*env)->DeleteLocalRef(env, activity);
    return view;
}

void mel_gui_canvas_platform_register(Mel_Atom atom)
{
    mel_gui_android_register_constructor(atom, mel__canvas_construct);
}
