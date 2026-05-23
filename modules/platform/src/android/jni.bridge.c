#include <core/platform.h>

#if !MEL_PLATFORM_ANDROID
    #error "android-only translation unit"
#endif

#include <platform/android/jni.h>

static JavaVM* g_vm;

JavaVM* mel_platform_android_vm(void) { return g_vm; }

JNIEnv* mel_platform_android_env(void)
{
    if (g_vm == NULL) return NULL;
    JNIEnv* env = NULL;
    if ((*g_vm)->GetEnv(g_vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        (*g_vm)->AttachCurrentThread(g_vm, &env, NULL);
    }
    return env;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved)
{
    (void)reserved;
    g_vm = vm;
    return JNI_VERSION_1_6;
}
