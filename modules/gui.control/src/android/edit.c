#include <gui.control/edit.h>
#include <gui.platform.android/gui.platform.android.h>

static jmethodID mel__a_create_edit;

static jobject mel__edit_construct(JNIEnv* env, jobject host, jclass host_class, Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc)
{
    MEL_UNUSED(h);
    if (mel__a_create_edit == NULL) {
        mel__a_create_edit = (*env)->GetMethodID(env, host_class, "createEdit", "(Ljava/lang/String;)Landroid/view/View;");
        if (mel__a_create_edit == NULL) return NULL;
    }
    jstring text = mel_gui_android_utf16(env, desc->text);
    jobject view = (*env)->CallObjectMethod(env, host, mel__a_create_edit, text);
    (*env)->DeleteLocalRef(env, text);
    return view;
}

void mel_gui_edit_platform_register(Mel_Atom atom)
{
    mel_gui_android_register_constructor(atom, mel__edit_construct);
}
