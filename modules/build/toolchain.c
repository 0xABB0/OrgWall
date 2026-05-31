#include "runner.h"

#include <stdio.h>

static char *capture(const char *cmd) {
    FILE *p = popen(cmd, "r");
    if (!p) return mel_str_dup("");
    char  *buf = malloc(1 << 12);
    size_t k   = fread(buf, 1, (1 << 12) - 1, p);
    pclose(p);
    while (k && (buf[k - 1] == '\n' || buf[k - 1] == ' ')) k--;
    buf[k] = 0;
    return buf;
}

static char *android_ndk(void) {
    const char *env = getenv("ANDROID_NDK_HOME");
    if (env && *env) return mel_str_dup(env);
    const char *home = getenv("HOME");
    if (!home) return mel_str_dup("");
    char *base = mel_str_fmt("%s/Library/Android/sdk/ndk", home);
    char *ls   = mel_str_fmt("ls -d %s/* 2>/dev/null | sort -V | tail -1", base);
    char *path = capture(ls);
    free(base);
    free(ls);
    return path;
}

Mel_Toolchain mel_toolchain(const Mel_Variant *v) {
    Mel_Toolchain tc = {
        .cc           = mel_str_dup("clang"),
        .ar           = mel_str_dup("ar"),
        .base_cflags  = mel_str_dup(""),
        .base_ldflags = mel_str_dup(""),
        .exe_ext      = "",
    };
    switch (v->platform) {
        case MEL_PLATFORM_MACOS:
            break;
        case MEL_PLATFORM_IOS: {
            char *sdk        = capture("xcrun --sdk iphoneos --show-sdk-path 2>/dev/null");
            tc.base_cflags   = mel_str_fmt("-target arm64-apple-ios13.0 -isysroot %s", sdk);
            tc.base_ldflags  = mel_str_dup(tc.base_cflags);
            free(sdk);
            break;
        }
        case MEL_PLATFORM_LINUX:
            free(tc.cc);
            free(tc.ar);
            free(tc.base_cflags);
            tc.cc          = mel_str_dup("zig cc -target x86_64-linux-gnu");
            tc.ar          = mel_str_dup("zig ar");
            tc.base_cflags = mel_str_dup("-D_GNU_SOURCE");
            break;
        case MEL_PLATFORM_WIN32:
            free(tc.cc);
            free(tc.ar);
            tc.cc      = mel_str_dup("zig cc -target x86_64-windows-gnu");
            tc.ar      = mel_str_dup("zig ar");
            tc.exe_ext = ".exe";
            break;
        case MEL_PLATFORM_ANDROID: {
            char *ndk = android_ndk();
            free(tc.cc);
            free(tc.ar);
            tc.cc = mel_str_fmt(
                "%s/toolchains/llvm/prebuilt/darwin-x86_64/bin/clang -target aarch64-linux-android24", ndk);
            tc.ar = mel_str_fmt("%s/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-ar", ndk);
            free(ndk);
            break;
        }
        case MEL_PLATFORM_WASM:
            free(tc.cc);
            free(tc.ar);
            tc.cc      = mel_str_dup("emcc");
            tc.ar      = mel_str_dup("emar");
            tc.exe_ext = ".js";
            break;
        default:
            break;
    }
    return tc;
}
