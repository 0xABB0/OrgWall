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

    // The gpu module needs a presentation surface; the wasi compute runtime has
    // none. The webgpu backend (default gpu axis on web) is brought up through
    // the emdawnwebgpu port, which is an emcc-only toolchain flag.
    mel_build_exclude_module_on_runtime(t, "wasi", "gpu");
    mel_build_add_cflag_on_runtime(t, MEL_PUBLIC, "emscripten", "--use-port=emdawnwebgpu");
    mel_build_add_link_flag_on_runtime(t, MEL_PUBLIC, "emscripten", "--use-port=emdawnwebgpu");
    mel_build_add_link_flag_on_runtime(t, MEL_PUBLIC, "emscripten",
                                       "--pre-js", "modules/gpu/src/webgpu/device_preinit.js");

    mel_build_add_dependency(t, "mongoose");
    mel_build_add_dependency(t, "sqlite3");
    mel_build_add_dependency(t, "sdl3");
    mel_build_add_dependency(t, "mpfr");
    mel_build_add_dependency(t, "gmp");

    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_MACOS, "-framework", "Cocoa");
    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_MACOS, "-framework", "CoreMIDI");
    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_MACOS, "-framework", "CoreFoundation");
    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_MACOS, "-framework", "Metal");
    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_MACOS, "-framework", "QuartzCore");

    // Vulkan loader, linked only when the vulkan gpu backend is selected. The NDK
    // supplies libvulkan on Android, Homebrew on macOS (MoltenVK is the driver),
    // system paths on Linux.
    mel_build_add_link_flag_on_gpu(t, MEL_PUBLIC, "vulkan", "-lvulkan");
    // surface.m is the Apple (Objective-C) Vulkan surface; toolchains without an
    // Objective-C runtime reject it (the NDK errors on -fobjc-arc). The vulkan
    // backend uses android_surface.c there instead.
    mel_build_exclude_source_on(t, MEL_PLATFORM_ANDROID, "surface.m");
    mel_build_exclude_source_on(t, MEL_PLATFORM_LINUX,   "surface.m");
    // Homebrew prefix for the macOS Vulkan headers/loader. Kept to macOS so it
    // never shadows the NDK's Vulkan headers on Android.
    mel_build_add_cflag_on(t, MEL_PUBLIC, MEL_PLATFORM_MACOS, "-I/opt/homebrew/include");
    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_MACOS, "-L/opt/homebrew/lib");

    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_IOS, "-framework", "UIKit");
    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_IOS, "-framework", "Foundation");
    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_IOS, "-framework", "CoreGraphics");
    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_IOS, "-framework", "QuartzCore");
    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_IOS, "-framework", "CoreFoundation");
    mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_IOS, "-framework", "Metal");

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
