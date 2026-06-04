#include <core/platform.h>

#if !MEL_PLATFORM_ANDROID
#error "android-only translation unit"
#endif

#include <platform/android/jni.h>

static JavaVM* g_vm;

static jobject   g_class_loader;
static jmethodID g_load_class;

JavaVM* mel_platform_android_vm(void) { return g_vm; }

JNIEnv* mel_platform_android_env(void)
{
    if (g_vm == NULL)
        return NULL;
    JNIEnv* env = NULL;
    if ((*g_vm)->GetEnv(g_vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK)
    {
        (*g_vm)->AttachCurrentThread(g_vm, &env, NULL);
    }
    return env;
}

jclass mel_platform_android_find_class(JNIEnv* env, const char* name)
{
    if (env == NULL || name == NULL)
        return NULL;

    if (g_class_loader != NULL && g_load_class != NULL)
    {
        char    dotted[256];
        usize   n = 0;
        for (const char* p = name; *p && n + 1 < sizeof(dotted); ++p, ++n)
            dotted[n] = (*p == '/') ? '.' : *p;
        dotted[n] = '\0';

        jstring jname = (*env)->NewStringUTF(env, dotted);
        jclass  cls = (jclass)(*env)->CallObjectMethod(env, g_class_loader, g_load_class, jname);
        (*env)->DeleteLocalRef(env, jname);
        if ((*env)->ExceptionCheck(env))
        {
            (*env)->ExceptionClear(env);
            cls = NULL;
        }
        if (cls != NULL)
            return cls;
    }

    jclass direct = (*env)->FindClass(env, name);
    if ((*env)->ExceptionCheck(env))
        (*env)->ExceptionClear(env);
    return direct;
}

static void capture_class_loader(JNIEnv* env)
{
    jclass anchor = (*env)->FindClass(env, "orgwall/melody/platform/MelodyActivity");
    if (anchor == NULL)
    {
        (*env)->ExceptionClear(env);
        return;
    }

    jclass    cls_cls = (*env)->GetObjectClass(env, anchor);
    jmethodID get_loader = (*env)->GetMethodID(env, cls_cls, "getClassLoader", "()Ljava/lang/ClassLoader;");
    jobject   loader = get_loader ? (*env)->CallObjectMethod(env, anchor, get_loader) : NULL;
    if (loader == NULL)
    {
        (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, anchor);
        return;
    }

    jclass    loader_cls = (*env)->GetObjectClass(env, loader);
    g_load_class = (*env)->GetMethodID(env, loader_cls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    g_class_loader = (*env)->NewGlobalRef(env, loader);

    (*env)->DeleteLocalRef(env, anchor);
    (*env)->DeleteLocalRef(env, loader);
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved)
{
    (void)reserved;
    g_vm = vm;
    JNIEnv* env = NULL;
    if ((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6) == JNI_OK && env != NULL)
        capture_class_loader(env);
    return JNI_VERSION_1_6;
}
