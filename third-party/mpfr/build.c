#include "build.h"
#include <stdio.h>

static bool build_mpfr(Mel_Build_Context *ctx) {
    char with_gmp[1024];
    snprintf(with_gmp, sizeof with_gmp, "--with-gmp=%s", mel_tp_dep_prefix(ctx, "gmp"));
    return mel_tp_autotools(ctx, "third-party/mpfr", with_gmp, "libmpfr.a");
}

bool project(Mel_Build_Target *t) {
    mel_build_set_name(t, "mpfr");
    mel_build_set_kind(t, MEL_TARGET_THIRD_PARTY);
    mel_build_add_dependency(t, "gmp");
    mel_build_add_link_flag(t, MEL_PUBLIC, "-lmpfr");
    mel_build_suppress_default(t, MEL_STAGE_COMPILE);
    mel_build_suppress_default(t, MEL_STAGE_LINK);
    mel_build_on_compile(t, build_mpfr);
    return true;
}
