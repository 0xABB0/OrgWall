#include "android.h"

void mel_gui__backend_canvas_create(Mel_Gui_Widget* w, str8 text)
{
    (void)text;
    JNIEnv* env = mel_gui__android_env();
    if (!env) return;

    Mel_Gui_Android* a = mel_gui__android();
    (*env)->CallStaticVoidMethod(env, a->cls, a->createCanvas,
        mel_gui__android_pack(w->self),
        mel_gui__android_pack(w->parent),
        w->x, w->y, w->width, w->height);

    w->native = (void*)(uintptr_t)1;
}
