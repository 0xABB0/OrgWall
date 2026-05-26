#include "build.h"

bool project(Mel_Build_Target *t) {
    mel_build_set_name(t, "melody");
    mel_build_set_kind(t, MEL_TARGET_LIBRARY);

    mel_build_add_modules(t, "modules");

    // gmp, mpfr and sqlite3 now cross-compile for both wasm runtimes, so the
    // arbitrary-precision math, music theory/midi and sqlite log sink build on
    // web. Only the server stays out: mongoose needs a listening socket, which
    // a browser/wasi sandbox can't provide.
    mel_build_exclude_module_on(t, MEL_PLATFORM_WEB, "server");

    // wasi is a DOM-less compute runtime; the emscripten DOM GUI backend can't
    // compile there. Keep the GUI on emscripten, drop it under wasi.
    mel_build_exclude_module_on_runtime(t, "wasi", "gui");

    mel_build_add_dependency(t, "mongoose");
    mel_build_add_dependency(t, "sqlite3");
    mel_build_add_dependency(t, "sdl3");
    mel_build_add_dependency(t, "mpfr");
    mel_build_add_dependency(t, "gmp");

    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_MACOS, "-framework", "Cocoa");
    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_MACOS, "-framework", "CoreMIDI");
    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_MACOS, "-framework", "CoreFoundation");

    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_IOS, "-framework", "UIKit");
    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_IOS, "-framework", "Foundation");
    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_IOS, "-framework", "CoreGraphics");
    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_IOS, "-framework", "QuartzCore");
    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_IOS, "-framework", "CoreFoundation");

    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_LINUX, "-lasound");
    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_LINUX, "-lpthread");
    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_LINUX, "-lm");

    mel_build_add_define_on(t, MEL_PUBLIC, MEL_PLATFORM_ANDROID, "ANDROID");
    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_ANDROID, "-llog");
    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_ANDROID, "-landroid");

    static const char *const win32_libs[] = {
        "-luser32", "-lgdi32", "-lcomctl32", "-lcomdlg32", "-lkernel32", "-lwinmm",
        "-Wl,/SUBSYSTEM:WINDOWS", "-Wl,/ENTRY:WinMainCRTStartup", "-Wl,/MANIFEST:NO",
    };
    for (size_t i = 0; i < sizeof(win32_libs) / sizeof(win32_libs[0]); i++) {
        mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_WIN32, win32_libs[i]);
    }
    return true;
}
