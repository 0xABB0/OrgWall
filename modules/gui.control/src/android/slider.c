#include <gui.control/slider.h>
#include <gui.platform.android/gui.platform.android.h>

static jmethodID mel__a_create_slider;

static jobject mel__slider_construct(JNIEnv* env, jobject host, jclass host_class, Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc)
{
    MEL_UNUSED(h);
    MEL_UNUSED(desc);
    if (mel__a_create_slider == NULL) {
        mel__a_create_slider = (*env)->GetMethodID(env, host_class, "createSlider", "()Landroid/view/View;");
        if (mel__a_create_slider == NULL) return NULL;
    }
    return (*env)->CallObjectMethod(env, host, mel__a_create_slider);
}

void mel_gui_slider_platform_register(Mel_Atom atom)
{
    mel_gui_android_register_constructor(atom, mel__slider_construct);
}
