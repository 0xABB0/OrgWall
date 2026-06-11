#include <messagebox/backend.h>

#include <platform/android/jni.h>
#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <log/log.h>

#include <string.h>

static jobject android_context(JNIEnv* env)
{
    jclass at = (*env)->FindClass(env, "android/app/ActivityThread");
    if (!at)
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    jmethodID cur = (*env)->GetStaticMethodID(env, at, "currentApplication", "()Landroid/app/Application;");
    if (!cur)
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    jobject app = (*env)->CallStaticObjectMethod(env, at, cur);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    return app;
}

static jstring jstr_from_str8(JNIEnv* env, str8 s)
{
    const Mel_Alloc* a = mel_alloc_heap();
    char*            c = (char*)mel_alloc(a, (usize)s.len + 1);
    if (!c)
        return (*env)->NewStringUTF(env, "");
    if (s.len > 0 && s.data)
        memcpy(c, s.data, (usize)s.len);
    c[s.len > 0 ? s.len : 0] = 0;
    jstring j = (*env)->NewStringUTF(env, c);
    mel_dealloc(a, c);
    return j;
}

static jint argb_of(Mel_Msgbox_Color col)
{
    return (jint)(((u32)col.value.a << 24) | ((u32)col.value.r << 16) | ((u32)col.value.g << 8) | (u32)col.value.b);
}

bool mel_msgbox__plat_available(void) { return mel_platform_android_env() != NULL; }

Mel_Msgbox_Status mel_msgbox__plat_show(const Mel_Msgbox_Request* req, i32* out_chosen_id)
{
    JNIEnv* env = mel_platform_android_env();
    if (!env || (*env)->PushLocalFrame(env, 32 + (jint)req->button_count) != 0)
    {
        *out_chosen_id = req->escape_id;
        return MEL_MSGBOX_ERROR | MEL_MSGBOX_RESULT_NO_BACKEND;
    }

    Mel_Msgbox_Status warn = 0;

    jobject context = req->native_parent ? (jobject)req->native_parent : android_context(env);
    if (!context)
    {
        (*env)->PopLocalFrame(env, NULL);
        mel_log_error("messagebox", "android: no Activity or Application context");
        *out_chosen_id = req->escape_id;
        return MEL_MSGBOX_ERROR | MEL_MSGBOX_RESULT_NO_BACKEND;
    }

    jclass helper = (*env)->FindClass(env, "orgwall/melody/messagebox/MelodyMessagebox");
    if (!helper)
    {
        (*env)->ExceptionClear(env);
        (*env)->PopLocalFrame(env, NULL);
        mel_log_error("messagebox", "android: MelodyMessagebox helper class not found");
        *out_chosen_id = req->escape_id;
        return MEL_MSGBOX_ERROR | MEL_MSGBOX_RESULT_NO_BACKEND;
    }
    jmethodID show = (*env)->GetStaticMethodID(
        env, helper, "show",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;IIZZIZIZI)I");
    if (!show)
    {
        (*env)->ExceptionClear(env);
        (*env)->PopLocalFrame(env, NULL);
        mel_log_error("messagebox", "android: MelodyMessagebox.show not found");
        *out_chosen_id = req->escape_id;
        return MEL_MSGBOX_ERROR;
    }

    u32 presented = req->button_count;
    if (presented > 3)
    {
        presented = 3;
        warn |= MEL_MSGBOX_WARN_BUTTONS_COLLAPSED;
    }

    jclass        string_cls = (*env)->FindClass(env, "java/lang/String");
    jobjectArray  labels = (*env)->NewObjectArray(env, (jsize)presented, string_cls, NULL);
    for (u32 i = 0; i < presented; i++)
    {
        str8 lbl = req->buttons[i].label.len > 0 ? req->buttons[i].label : (str8){ (u8*)"OK", 2 };
        jstring js = jstr_from_str8(env, lbl);
        (*env)->SetObjectArrayElement(env, labels, (jsize)i, js);
    }

    jint default_idx = 0;
    jint cancel_idx = -1;
    for (u32 i = 0; i < presented; i++)
    {
        if (req->buttons[i].id == req->default_id)
            default_idx = (jint)i;
        if (req->buttons[i].id == req->escape_id)
            cancel_idx = (jint)i;
    }

    jstring jtitle = jstr_from_str8(env, req->title);
    jstring jmsg = jstr_from_str8(env, req->message);

    jint chosen_idx = (*env)->CallStaticIntMethod(
        env, helper, show, context, jtitle, jmsg, labels, default_idx, cancel_idx,
        (jboolean)(req->right_to_left ? JNI_TRUE : JNI_FALSE),
        (jboolean)(req->accent.has_value ? JNI_TRUE : JNI_FALSE), argb_of(req->accent),
        (jboolean)(req->text.has_value ? JNI_TRUE : JNI_FALSE), argb_of(req->text),
        (jboolean)(req->background.has_value ? JNI_TRUE : JNI_FALSE), argb_of(req->background));

    Mel_Msgbox_Status st = MEL_MSGBOX_OK;
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        st = MEL_MSGBOX_ERROR;
    }

    i32 chosen = req->escape_id;
    if (chosen_idx >= 0 && (u32)chosen_idx < presented)
        chosen = req->buttons[chosen_idx].id;
    else
        st |= MEL_MSGBOX_RESULT_DISMISSED;

    (*env)->PopLocalFrame(env, NULL);

    *out_chosen_id = chosen;
    return st | warn | (warn && (st & MEL_MSGBOX_SEVERITY_MASK) == MEL_MSGBOX_OK ? MEL_MSGBOX_WARNED : MEL_MSGBOX_OK);
}
