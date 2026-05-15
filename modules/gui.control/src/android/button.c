#include <gui.control/button.h>
#include <gui.platform.android/gui.platform.android.h>

static jmethodID mel__a_create_button;

static jobject mel__button_construct(JNIEnv* env, jobject host, jclass host_class, Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc)
{
    MEL_UNUSED(h);
    if (mel__a_create_button == NULL) {
        mel__a_create_button = (*env)->GetMethodID(env, host_class, "createButton", "(Ljava/lang/String;)Landroid/view/View;");
        if (mel__a_create_button == NULL) return NULL;
    }
    jstring text = mel_gui_android_utf16(env, desc->text);
    jobject view = (*env)->CallObjectMethod(env, host, mel__a_create_button, text);
    (*env)->DeleteLocalRef(env, text);
    return view;
}

void mel_gui_button_platform_register(Mel_Atom atom)
{
    mel_gui_android_register_constructor(atom, mel__button_construct);
}
