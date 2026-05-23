#include <core/platform.h>

#if !MEL_PLATFORM_ANDROID
    #error "android-only translation unit"
#endif

#include <jni.h>

#include <app/app.h>
#include <gui/init.h>
#include <reactor/reactor.h>

static bool app_init(Mel_Reactor* reactor, void* user)
{
    (void)user;
    mel_app_setup(reactor);
    return true;
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelGui_nativeStart(JNIEnv* env, jclass cls)
{
    (void)env; (void)cls;
    mel_reactor_spawn(MEL_REACTOR_ATTACHED, app_init, NULL);
}

JNIEXPORT void JNICALL
Java_orgwall_melody_platform_MelGui_nativeStop(JNIEnv* env, jclass cls)
{
    (void)env; (void)cls;
    mel_gui_shutdown();
}
