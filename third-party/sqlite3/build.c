#include "build.h"

static bool build_sqlite3(Mel_Build_Context *ctx) {
    static const char *const cflags[] = {
        "-DSQLITE_OMIT_LOAD_EXTENSION",
        "-DSQLITE_THREADSAFE=1",
    };
    static const char *const headers[] = {
        "third-party/sqlite3/sqlite3.h",
        "third-party/sqlite3/sqlite3ext.h",
    };
    return mel_tp_single_tu(ctx, "third-party/sqlite3/sqlite3.c",
                            cflags, 2, headers, 2);
}

bool project(Mel_Build_Target *t) {
    mel_build_set_name(t, "sqlite3");
    mel_build_set_kind(t, MEL_TARGET_THIRD_PARTY);
    static const Mel_Platform native_only[] = {
        MEL_PLATFORM_MACOS, MEL_PLATFORM_IOS, MEL_PLATFORM_LINUX,
        MEL_PLATFORM_ANDROID, MEL_PLATFORM_WIN32,
    };
    mel_build_set_platforms(t, native_only, 5);
    mel_build_add_link_flag(t, MEL_PUBLIC, "-lsqlite3");
    mel_build_suppress_default(t, MEL_STAGE_COMPILE);
    mel_build_suppress_default(t, MEL_STAGE_LINK);
    mel_build_on_compile(t, build_sqlite3);
    return true;
}
