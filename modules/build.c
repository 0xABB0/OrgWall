#include "build.h"

bool project(Mel_Build_Target *t) {
    mel_build_set_name(t, "melody");
    mel_build_set_kind(t, MEL_TARGET_LIBRARY);

    mel_build_add_modules(t, "modules");

    // Static linkage order matters: mpfr depends on gmp.
    mel_build_add_dependency(t, "mongoose");
    mel_build_add_dependency(t, "sqlite3");
    mel_build_add_dependency(t, "sdl3");
    mel_build_add_dependency(t, "mpfr");
    mel_build_add_dependency(t, "gmp");

    // System libraries/frameworks that consumers linking melody require,
    // gated per target platform (resolved at the consumer's link).
    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_MACOS, "-framework");
    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_MACOS, "Cocoa");
    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_MACOS, "-framework");
    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_MACOS, "CoreMIDI");
    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_MACOS, "-framework");
    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_MACOS, "CoreFoundation");

    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_LINUX, "-lasound");
    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_LINUX, "-lpthread");
    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_LINUX, "-lm");

    // Android links a single shared object; system libs the NDK provides.
    mel_build_add_define_on(t, MEL_PUBLIC, MEL_PLATFORM_ANDROID, "ANDROID");
    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_ANDROID, "-llog");
    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_ANDROID, "-landroid");

    // Win32 desktop system libraries and subsystem/entry linker flags.
    static const char *const win32_libs[] = {
        "-luser32", "-lgdi32", "-lcomctl32", "-lcomdlg32", "-lkernel32", "-lwinmm",
        "-Wl,/SUBSYSTEM:WINDOWS", "-Wl,/ENTRY:WinMainCRTStartup", "-Wl,/MANIFEST:NO",
    };
    for (size_t i = 0; i < sizeof(win32_libs) / sizeof(win32_libs[0]); i++) {
        mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_WIN32, win32_libs[i]);
    }
    return true;
}
