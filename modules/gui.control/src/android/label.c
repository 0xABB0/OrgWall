#include <gui.control/label.h>
#include <gui.platform.android/gui.platform.android.h>

static jmethodID mel__a_create_label;

static jobject mel__label_construct(JNIEnv* env, jobject host, jclass host_class, Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc)
{
    MEL_UNUSED(h);
    if (mel__a_create_label == NULL) {
        mel__a_create_label = (*env)->GetMethodID(env, host_class, "createLabel", "(Ljava/lang/String;I)Landroid/view/View;");
        if (mel__a_create_label == NULL) return NULL;
    }
    jstring text = mel_gui_android_utf16(env, desc->text);
    jobject view = (*env)->CallObjectMethod(env, host, mel__a_create_label, text, (jint)desc->id);
    (*env)->DeleteLocalRef(env, text);
    return view;
}

void mel_gui_label_platform_register(Mel_Atom atom)
{
    mel_gui_android_register_constructor(atom, mel__label_construct);
}
