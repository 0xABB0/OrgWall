#include "build.h"

static bool build_mongoose(Mel_Build_Context *ctx) {
    static const char *const headers[] = { "third-party/mongoose/mongoose.h" };
    return mel_tp_single_tu(ctx, "third-party/mongoose/mongoose.c",
                            NULL, 0, headers, 1);
}

bool project(Mel_Build_Target *t) {
    mel_build_set_name(t, "mongoose");
    mel_build_set_kind(t, MEL_TARGET_THIRD_PARTY);
    mel_build_add_link_flag(t, MEL_PUBLIC, "-lmongoose");
    mel_build_suppress_default(t, MEL_STAGE_COMPILE);
    mel_build_suppress_default(t, MEL_STAGE_LINK);
    mel_build_on_compile(t, build_mongoose);
    return true;
}
