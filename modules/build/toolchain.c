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

static const char *llvm_arch(const char *arch) {
    if (arch && strcmp(arch, "arm64") == 0) return "aarch64";
    return arch ? arch : "x86_64";
}

Mel_Toolchain mel_toolchain(const Mel_Variant *v) {
    Mel_Toolchain tc = {
        .cc           = mel_str_dup("clang"),
        .ar           = mel_str_dup("ar"),
        .base_cflags  = mel_str_dup(""),
        .base_ldflags = mel_str_dup(""),
        .exe_ext      = "",
        .triple       = "arm64-apple-darwin",
        .cross        = false,
    };
    const char *la = llvm_arch(v->arch);
    switch (v->platform) {
        case MEL_PLATFORM_MACOS:
            free(tc.base_cflags);
            free(tc.base_ldflags);
            tc.base_cflags  = mel_str_fmt("-arch %s", v->arch);
            tc.base_ldflags = mel_str_fmt("-arch %s", v->arch);
            tc.triple       = mel_str_fmt("%s-apple-darwin", v->arch);
            break;
        case MEL_PLATFORM_IOS: {
            char *sdk       = capture("xcrun --sdk iphoneos --show-sdk-path 2>/dev/null");
            tc.base_cflags  = mel_str_fmt("-target %s-apple-ios13.0 -isysroot %s", v->arch, sdk);
            tc.base_ldflags = mel_str_dup(tc.base_cflags);
            tc.triple       = mel_str_fmt("%s-apple-darwin", v->arch);
            tc.cross        = true;
            free(sdk);
            break;
        }
        case MEL_PLATFORM_LINUX:
            free(tc.cc);
            free(tc.ar);
            free(tc.base_cflags);
            tc.cc          = mel_str_fmt("zig cc -target %s-linux-gnu", la);
            tc.ar          = mel_str_dup("zig ar");
            tc.base_cflags = mel_str_dup("-D_GNU_SOURCE");
            tc.triple      = mel_str_fmt("%s-linux-gnu", la);
            tc.cross       = true;
            break;
        case MEL_PLATFORM_WIN32:
            free(tc.cc);
            free(tc.ar);
            tc.cc           = mel_str_fmt("zig cc -target %s-windows-gnu", la);
            tc.ar           = mel_str_dup("zig ar");
            tc.autotools_cc = mel_str_fmt("%s-w64-mingw32-gcc", la);
            tc.exe_ext      = ".exe";
            tc.triple       = mel_str_fmt("%s-w64-mingw32", la);
            tc.cross        = true;
            break;
        case MEL_PLATFORM_ANDROID: {
            char *ndk = android_ndk();
            free(tc.cc);
            free(tc.ar);
            tc.cc     = mel_str_fmt(
                "%s/toolchains/llvm/prebuilt/darwin-x86_64/bin/clang -target %s-linux-android24", ndk, la);
            tc.ar     = mel_str_fmt("%s/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-ar", ndk);
            tc.triple = mel_str_fmt("%s-linux-android", la);
            tc.cross  = true;
            free(ndk);
            break;
        }
        case MEL_PLATFORM_WASM:
            free(tc.cc);
            free(tc.ar);
            tc.cc      = mel_str_dup("emcc");
            tc.ar      = mel_str_dup("emar");
            tc.exe_ext = ".js";
            tc.triple  = "wasm32";
            tc.cross   = true;
            break;
        default:
            break;
    }
    if (!tc.autotools_cc) tc.autotools_cc = mel_str_dup(tc.cc);
    return tc;
}
