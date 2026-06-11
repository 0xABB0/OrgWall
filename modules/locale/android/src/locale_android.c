#include <locale/provider.h>

#include <platform/android/jni.h>
#include <log/log.h>

#include <allocator/allocator.h>

#include <string.h>

static bool tag_from_locale(JNIEnv* env, jobject locale, const Mel_Alloc* alloc, Mel_Locale_Raw* out)
{
    jclass    lc = (*env)->GetObjectClass(env, locale);
    jmethodID to_tag = (*env)->GetMethodID(env, lc, "toLanguageTag", "()Ljava/lang/String;");
    if (!to_tag)
    {
        (*env)->ExceptionClear(env);
        return false;
    }
    jstring js = (jstring)(*env)->CallObjectMethod(env, locale, to_tag);
    if ((*env)->ExceptionCheck(env) || !js)
    {
        (*env)->ExceptionClear(env);
        return false;
    }
    const char* utf8 = (*env)->GetStringUTFChars(env, js, NULL);
    if (!utf8)
        return false;
    usize len = strlen(utf8);
    bool  ok = false;
    if (len > 0)
    {
        u8* buf = (u8*)mel_alloc(alloc, len);
        if (buf)
        {
            memcpy(buf, utf8, len);
            *out = (Mel_Locale_Raw){ .tag = { .data = buf, .len = (size)len } };
            ok = true;
        }
    }
    (*env)->ReleaseStringUTFChars(env, js, utf8);
    return ok;
}

static u32 android_enumerate(void* user, const Mel_Alloc* alloc, Mel_Locale_Raw* out, u32 cap)
{
    (void)user;
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return 0;
    if ((*env)->PushLocalFrame(env, 32) != 0)
        return 0;

    u32    produced = 0;
    jclass ll = (*env)->FindClass(env, "android/os/LocaleList");
    if (ll)
    {
        jmethodID get_default = (*env)->GetStaticMethodID(env, ll, "getDefault", "()Landroid/os/LocaleList;");
        jobject   list = get_default ? (*env)->CallStaticObjectMethod(env, ll, get_default) : NULL;
        if ((*env)->ExceptionCheck(env))
        {
            (*env)->ExceptionClear(env);
            list = NULL;
        }
        if (list)
        {
            jmethodID size_m = (*env)->GetMethodID(env, ll, "size", "()I");
            jmethodID get_m = (*env)->GetMethodID(env, ll, "get", "(I)Ljava/util/Locale;");
            jint      n = (size_m && get_m) ? (*env)->CallIntMethod(env, list, size_m) : 0;
            if ((u32)n > cap)
            {
                (*env)->PopLocalFrame(env, NULL);
                return (u32)n;
            }
            for (jint i = 0; i < n && produced < cap; i++)
            {
                jobject loc = (*env)->CallObjectMethod(env, list, get_m, i);
                if (loc && tag_from_locale(env, loc, alloc, &out[produced]))
                    produced++;
                if (loc)
                    (*env)->DeleteLocalRef(env, loc);
            }
        }
    }
    else
        (*env)->ExceptionClear(env);

    if (produced == 0 && cap > 0)
    {
        jclass jl = (*env)->FindClass(env, "java/util/Locale");
        if (jl)
        {
            jmethodID get_default = (*env)->GetStaticMethodID(env, jl, "getDefault", "()Ljava/util/Locale;");
            jobject   loc = get_default ? (*env)->CallStaticObjectMethod(env, jl, get_default) : NULL;
            if (loc && tag_from_locale(env, loc, alloc, &out[0]))
                produced = 1;
        }
        else
            (*env)->ExceptionClear(env);
    }

    (*env)->PopLocalFrame(env, NULL);
    return produced;
}

void mel_locale__register_host_providers(void)
{
    static const Mel_Locale_Provider_Desc desc = {
        .name = "android-localelist",
        .enumerate = android_enumerate,
    };
    mel_locale_provider_register(&desc);
}
