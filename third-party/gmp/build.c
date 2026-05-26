#include "build.h"

static bool build_gmp(Mel_Build_Context *ctx) {
    // libtool produces MSVC-style names (`gmp.lib`) when configured against
    // clang-cl with no `--host`; everywhere else it's the GNU `libgmp.a`.
    const char *lib = (mel_build_ctx_platform(ctx) == MEL_PLATFORM_WIN32)
        ? "gmp.lib" : "libgmp.a";
    return mel_tp_autotools(ctx, "third-party/gmp", NULL, lib);
}

bool project(Mel_Build_Target *t) {
    mel_build_set_name(t, "gmp");
    mel_build_set_kind(t, MEL_TARGET_THIRD_PARTY);
    static const Mel_Platform native_only[] = {
        MEL_PLATFORM_MACOS, MEL_PLATFORM_IOS, MEL_PLATFORM_LINUX,
        MEL_PLATFORM_ANDROID, MEL_PLATFORM_WIN32,
    };
    mel_build_set_platforms(t, native_only, 5);
    mel_build_add_link_flag(t, MEL_PUBLIC, "-lgmp");
    mel_build_suppress_default(t, MEL_STAGE_COMPILE);
    mel_build_suppress_default(t, MEL_STAGE_LINK);
    mel_build_on_compile(t, build_gmp);
    return true;
}
