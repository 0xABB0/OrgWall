#include <gui.control/panel.h>
#include <gui.platform.android/gui.platform.android.h>

static jmethodID mel__a_create_panel;

static jobject mel__panel_construct(JNIEnv* env, jobject host, jclass host_class, Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc)
{
    MEL_UNUSED(h);
    MEL_UNUSED(desc);
    if (mel__a_create_panel == NULL) {
        mel__a_create_panel = (*env)->GetMethodID(env, host_class, "createPanel", "()Landroid/view/View;");
        if (mel__a_create_panel == NULL) return NULL;
    }
    return (*env)->CallObjectMethod(env, host, mel__a_create_panel);
}

void mel_gui_panel_platform_register(Mel_Atom atom)
{
    mel_gui_android_register_constructor(atom, mel__panel_construct);
}
