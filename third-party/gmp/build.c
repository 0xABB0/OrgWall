#include "build.h"

static bool build_gmp(Mel_Build_Context *ctx) {
    return mel_tp_autotools(ctx, "third-party/gmp", NULL, "libgmp.a");
}

bool project(Mel_Build_Target *t) {
    mel_build_set_name(t, "gmp");
    mel_build_set_kind(t, MEL_TARGET_THIRD_PARTY);
    mel_build_add_link_flag(t, MEL_PUBLIC, "-lgmp");
    mel_build_suppress_default(t, MEL_STAGE_COMPILE);
    mel_build_suppress_default(t, MEL_STAGE_LINK);
    mel_build_on_compile(t, build_gmp);
    return true;
}
