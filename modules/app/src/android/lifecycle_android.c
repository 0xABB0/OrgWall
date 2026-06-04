#include <core/platform.h>

#if !MEL_PLATFORM_ANDROID
#error "android-only translation unit"
#endif

#include <jni.h>
#include <stdint.h>

#include <app/provider.h>
#include <reactor/reactor.h>
#include <log/log.h>

static void plat_start(void* user) { (void)user; }
static void plat_stop(void* user) { (void)user; }

void mel_app__register_platform_provider(void)
{
    Mel_App_Provider_Desc desc = { .name = "android-activity", .start = plat_start, .stop = plat_stop };
    mel_app_provider_register(&desc);
}

static void emit_on_loop(void* user)
{
    mel_app__emit((u32)(uintptr_t)user);
}

static void marshal_phase(u32 phase)
{
    Mel_Reactor* reactor = mel_app__reactor();
    if (reactor == NULL)
    {
        mel_log_warn("app", "android lifecycle: no reactor; phase %u dropped", phase);
        return;
    }
    mel_reactor_post(reactor, emit_on_loop, (void*)(uintptr_t)phase);
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_MelGui_nativeOnResume(JNIEnv* env, jclass cls)
{
    (void)env;
    (void)cls;
    marshal_phase(MEL_APP_PHASE_WILL_ENTER_FOREGROUND | MEL_APP_PHASE_DID_BECOME_ACTIVE);
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_MelGui_nativeOnPause(JNIEnv* env, jclass cls)
{
    (void)env;
    (void)cls;
    marshal_phase(MEL_APP_PHASE_WILL_RESIGN_ACTIVE);
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_MelGui_nativeOnStop(JNIEnv* env, jclass cls)
{
    (void)env;
    (void)cls;
    marshal_phase(MEL_APP_PHASE_DID_ENTER_BACKGROUND);
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_MelGui_nativeOnDestroy(JNIEnv* env, jclass cls)
{
    (void)env;
    (void)cls;
    marshal_phase(MEL_APP_PHASE_WILL_TERMINATE);
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_MelGui_nativeOnLowMemory(JNIEnv* env, jclass cls)
{
    (void)env;
    (void)cls;
    marshal_phase(MEL_APP_PHASE_LOW_MEMORY);
}
