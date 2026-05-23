#include "android.h"

void mel_gui__backend_button_create(Mel_Gui_Widget* w, str8 text)
{
    JNIEnv* env = mel_gui__android_env();
    if (!env) return;

    Mel_Gui_Android* a = mel_gui__android();
    jstring s = mel_gui__android_jstring(env, text);
    (*env)->CallStaticVoidMethod(env, a->cls, a->createButton,
        mel_gui__android_pack(w->self),
        mel_gui__android_pack(w->parent),
        w->x, w->y, w->width, w->height,
        s);
    (*env)->DeleteLocalRef(env, s);

    w->native = (void*)(uintptr_t)1;
}
