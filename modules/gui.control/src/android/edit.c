#include <gui.control/edit.h>
#include <gui.platform.android/gui.platform.android.h>

#define MEL_A_FG_COLOR    0xFFF5F1E8u
#define MEL_A_HINT_COLOR  0xFFAAB2BAu
#define MEL_A_EDIT_BG     0xFF263341u

static jclass    mel__a_edit_class;
static jmethodID mel__a_edit_ctor;
static jmethodID mel__a_edit_set_text;
static jmethodID mel__a_edit_set_single_line;
static jmethodID mel__a_edit_set_text_color;
static jmethodID mel__a_edit_set_hint_color;
static jmethodID mel__a_edit_set_text_size;
static jmethodID mel__a_edit_set_select_all;
static jmethodID mel__a_edit_set_bg_color;
static jmethodID mel__a_edit_add_watcher;

static void mel__a_edit_ensure(JNIEnv* env)
{
    if (mel__a_edit_class != NULL) return;
    jclass local = (*env)->FindClass(env, "android/widget/EditText");
    mel__a_edit_class           = (*env)->NewGlobalRef(env, local);
    (*env)->DeleteLocalRef(env, local);
    mel__a_edit_ctor            = (*env)->GetMethodID(env, mel__a_edit_class, "<init>",                 "(Landroid/content/Context;)V");
    mel__a_edit_set_text        = (*env)->GetMethodID(env, mel__a_edit_class, "setText",                "(Ljava/lang/CharSequence;)V");
    mel__a_edit_set_single_line = (*env)->GetMethodID(env, mel__a_edit_class, "setSingleLine",          "(Z)V");
    mel__a_edit_set_text_color  = (*env)->GetMethodID(env, mel__a_edit_class, "setTextColor",           "(I)V");
    mel__a_edit_set_hint_color  = (*env)->GetMethodID(env, mel__a_edit_class, "setHintTextColor",       "(I)V");
    mel__a_edit_set_text_size   = (*env)->GetMethodID(env, mel__a_edit_class, "setTextSize",            "(F)V");
    mel__a_edit_set_select_all  = (*env)->GetMethodID(env, mel__a_edit_class, "setSelectAllOnFocus",    "(Z)V");
    mel__a_edit_set_bg_color    = (*env)->GetMethodID(env, mel__a_edit_class, "setBackgroundColor",     "(I)V");
    mel__a_edit_add_watcher     = (*env)->GetMethodID(env, mel__a_edit_class, "addTextChangedListener", "(Landroid/text/TextWatcher;)V");
}

static jobject mel__edit_construct(JNIEnv* env, jobject host, jclass host_class, Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc)
{
    MEL_UNUSED(host);
    MEL_UNUSED(host_class);
    mel__a_edit_ensure(env);

    jobject activity = mel_gui_android_activity(env);
    jobject view = (*env)->NewObject(env, mel__a_edit_class, mel__a_edit_ctor, activity);
    (*env)->DeleteLocalRef(env, activity);

    jstring text = mel_gui_android_utf16(env, desc->text);
    (*env)->CallVoidMethod(env, view, mel__a_edit_set_text, text);
    (*env)->DeleteLocalRef(env, text);

    (*env)->CallVoidMethod(env, view, mel__a_edit_set_single_line, JNI_TRUE);
    (*env)->CallVoidMethod(env, view, mel__a_edit_set_text_color,  (jint)MEL_A_FG_COLOR);
    (*env)->CallVoidMethod(env, view, mel__a_edit_set_hint_color,  (jint)MEL_A_HINT_COLOR);
    (*env)->CallVoidMethod(env, view, mel__a_edit_set_text_size,   15.0f);
    (*env)->CallVoidMethod(env, view, mel__a_edit_set_select_all,  JNI_FALSE);
    (*env)->CallVoidMethod(env, view, mel__a_edit_set_bg_color,    (jint)MEL_A_EDIT_BG);

    jobject listener = mel_gui_android_new_listener(env, h);
    (*env)->CallVoidMethod(env, view, mel__a_edit_add_watcher, listener);
    mel_gui_android_bind_edit_watcher(env, view, listener);
    mel_gui_android_install_focus_listener(env, view, listener);
    (*env)->DeleteLocalRef(env, listener);

    return view;
}

void mel_gui_edit_platform_register(Mel_Atom atom)
{
    mel_gui_android_register_constructor(atom, mel__edit_construct);
}
