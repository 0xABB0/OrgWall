#include <core/platform.h>

#if !MEL_PLATFORM_ANDROID
#error "android-only translation unit"
#endif

#include <jni.h>

#include <app/provider.h>

static void plat_start(void* user) { (void)user; }
static void plat_stop(void* user) { (void)user; }

void mel_app__register_platform_provider(void)
{
    Mel_App_Provider_Desc desc = { .name = "android-activity", .start = plat_start, .stop = plat_stop };
    mel_app_provider_register(&desc);
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_MelGui_nativeOnResume(JNIEnv* env, jclass cls)
{
    (void)env;
    (void)cls;
    mel_app__emit(MEL_APP_PHASE_WILL_ENTER_FOREGROUND | MEL_APP_PHASE_DID_BECOME_ACTIVE);
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_MelGui_nativeOnPause(JNIEnv* env, jclass cls)
{
    (void)env;
    (void)cls;
    mel_app__emit(MEL_APP_PHASE_WILL_RESIGN_ACTIVE);
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_MelGui_nativeOnStop(JNIEnv* env, jclass cls)
{
    (void)env;
    (void)cls;
    mel_app__emit(MEL_APP_PHASE_DID_ENTER_BACKGROUND);
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_MelGui_nativeOnDestroy(JNIEnv* env, jclass cls)
{
    (void)env;
    (void)cls;
    mel_app__emit(MEL_APP_PHASE_WILL_TERMINATE);
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_MelGui_nativeOnLowMemory(JNIEnv* env, jclass cls)
{
    (void)env;
    (void)cls;
    mel_app__emit(MEL_APP_PHASE_LOW_MEMORY);
}
