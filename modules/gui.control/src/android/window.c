#include <gui.control/window.h>
#include <gui.platform.android/gui.platform.android.h>

static jmethodID mel__a_create_window;

static jobject mel__window_construct(JNIEnv* env, jobject host, jclass host_class, Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc)
{
    MEL_UNUSED(h);
    MEL_UNUSED(desc);
    if (mel__a_create_window == NULL) {
        mel__a_create_window = (*env)->GetMethodID(env, host_class, "createWindow", "()Landroid/view/View;");
        if (mel__a_create_window == NULL) return NULL;
    }
    return (*env)->CallObjectMethod(env, host, mel__a_create_window);
}

void mel_gui_window_platform_register(Mel_Atom atom)
{
    mel_gui_android_register_constructor(atom, mel__window_construct);
}
