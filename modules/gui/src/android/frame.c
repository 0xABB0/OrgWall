#include "android.h"

void mel_gui__backend_frame_create(Mel_Gui_Widget* w, str8 title)
{
    JNIEnv* env = mel_gui__android_env();
    if (!env) return;

    Mel_Gui_Android* a = mel_gui__android();
    jstring s = mel_gui__android_jstring(env, title);
    (*env)->CallStaticVoidMethod(env, a->cls, a->createFrame,
        mel_gui__android_pack(w->self), s);
    (*env)->DeleteLocalRef(env, s);

    w->native = (void*)(uintptr_t)1;
    mel_gui__frames_inc();
}
