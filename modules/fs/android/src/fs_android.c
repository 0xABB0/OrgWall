#include "../../src/posix/fs_posix_ops.inl"

#include <platform/android/jni.h>

#include <allocator/allocator.h>
#include <string/str8.h>
#include <string/path.h>

typedef struct
{
    str8        org;
    str8        app;
    const char* bundle_id;
    bool        set;
} Fs_Pref_Identity;

static Fs_Pref_Identity g_pref;

void mel_fs_pref_identity_opt(Mel_Fs_Pref_Opt opt)
{
    g_pref.org = opt.org;
    g_pref.app = opt.app;
    g_pref.bundle_id = opt.bundle_id;
    g_pref.set = true;
}

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

static Mel_Fs_Path_Result path_from_jfile(JNIEnv* env, jobject file, const Mel_Alloc* alloc)
{
    Mel_Fs_Path_Result r = { 0 };
    if (!file)
    {
        r.status = MEL_FS_ERROR | MEL_FS_UNAVAILABLE;
        return r;
    }
    jclass    fcls = (*env)->GetObjectClass(env, file);
    jmethodID gap = (*env)->GetMethodID(env, fcls, "getAbsolutePath", "()Ljava/lang/String;");
    jstring   js = gap ? (jstring)(*env)->CallObjectMethod(env, file, gap) : NULL;
    if (!js)
    {
        (*env)->ExceptionClear(env);
        r.status = MEL_FS_ERROR | MEL_FS_UNAVAILABLE;
        return r;
    }
    const char* c = (*env)->GetStringUTFChars(env, js, NULL);
    r.value = str8_dup_alloc(str8_from_cstr(c), alloc);
    r.status = MEL_FS_OK;
    (*env)->ReleaseStringUTFChars(env, js, c);
    return r;
}

static Mel_Fs_Path_Result ctx_dir(JNIEnv* env, jobject ctx, const char* method, const Mel_Alloc* alloc)
{
    jclass    ccls = (*env)->FindClass(env, "android/content/Context");
    jmethodID m = (*env)->GetMethodID(env, ccls, method, "()Ljava/io/File;");
    if (!m)
    {
        (*env)->ExceptionClear(env);
        Mel_Fs_Path_Result r = { 0 };
        r.status = MEL_FS_ERROR | MEL_FS_UNAVAILABLE;
        return r;
    }
    jobject file = (*env)->CallObjectMethod(env, ctx, m);
    return path_from_jfile(env, file, alloc);
}

static Mel_Fs_Path_Result ctx_external_files(JNIEnv* env, jobject ctx, const char* type, const Mel_Alloc* alloc)
{
    jclass    ccls = (*env)->FindClass(env, "android/content/Context");
    jmethodID m = (*env)->GetMethodID(env, ccls, "getExternalFilesDir", "(Ljava/lang/String;)Ljava/io/File;");
    if (!m)
    {
        (*env)->ExceptionClear(env);
        Mel_Fs_Path_Result r = { 0 };
        r.status = MEL_FS_ERROR | MEL_FS_UNAVAILABLE;
        return r;
    }
    jstring js = type ? (*env)->NewStringUTF(env, type) : NULL;
    jobject file = (*env)->CallObjectMethod(env, ctx, m, js);
    return path_from_jfile(env, file, alloc);
}

Mel_Fs_Path_Result mel_fs__backend_folder(Mel_Fs_Folder folder, const Mel_Alloc* alloc)
{
    JNIEnv* env = mel_platform_android_env();
    if (!env)
    {
        Mel_Fs_Path_Result r = { 0 };
        r.status = MEL_FS_ERROR | MEL_FS_UNAVAILABLE;
        return r;
    }
    jobject ctx = android_context(env);
    if (!ctx)
    {
        Mel_Fs_Path_Result r = { 0 };
        r.status = MEL_FS_ERROR | MEL_FS_UNAVAILABLE;
        return r;
    }

    switch (folder)
    {
    case MEL_FS_FOLDER_BASE:
    case MEL_FS_FOLDER_PREF:
        return ctx_dir(env, ctx, "getFilesDir", alloc);
    case MEL_FS_FOLDER_CACHE:
    case MEL_FS_FOLDER_TEMP:
        return ctx_dir(env, ctx, "getCacheDir", alloc);
    case MEL_FS_FOLDER_HOME:
    case MEL_FS_FOLDER_DOCUMENTS:
    case MEL_FS_FOLDER_SAVED_GAMES:
    case MEL_FS_FOLDER_TEMPLATES:
        return ctx_external_files(env, ctx, NULL, alloc);
    case MEL_FS_FOLDER_MUSIC:
        return ctx_external_files(env, ctx, "Music", alloc);
    case MEL_FS_FOLDER_PICTURES:
    case MEL_FS_FOLDER_SCREENSHOTS:
        return ctx_external_files(env, ctx, "Pictures", alloc);
    case MEL_FS_FOLDER_VIDEOS:
        return ctx_external_files(env, ctx, "Movies", alloc);
    case MEL_FS_FOLDER_DOWNLOADS:
        return ctx_external_files(env, ctx, "Download", alloc);
    case MEL_FS_FOLDER_DESKTOP:
    default:
    {
        Mel_Fs_Path_Result r = { 0 };
        r.status = MEL_FS_ERROR | MEL_FS_UNAVAILABLE;
        return r;
    }
    }
}
