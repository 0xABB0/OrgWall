#include "runner.h"

#include <stdio.h>

static Mel_Platform host_platform(void) {
#if defined(__APPLE__)
    return MEL_PLATFORM_MACOS;
#elif defined(_WIN32)
    return MEL_PLATFORM_WIN32;
#else
    return MEL_PLATFORM_LINUX;
#endif
}

static bool tok_is(const char *tok, size_t n, const char *name) {
    return strlen(name) == n && strncmp(tok, name, n) == 0;
}

static bool parse_platform(const char *tok, Mel_Platform *out) {
    size_t n = strcspn(tok, ":");
    if (tok_is(tok, n, "macos")) return *out = MEL_PLATFORM_MACOS, true;
    if (tok_is(tok, n, "ios")) return *out = MEL_PLATFORM_IOS, true;
    if (tok_is(tok, n, "linux")) return *out = MEL_PLATFORM_LINUX, true;
    if (tok_is(tok, n, "android")) return *out = MEL_PLATFORM_ANDROID, true;
    if (tok_is(tok, n, "win32")) return *out = MEL_PLATFORM_WIN32, true;
    if (tok_is(tok, n, "wasm")) return *out = MEL_PLATFORM_WASM, true;
    return false;
}

int mel_build_main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: nob build <target> [platform[:backend[:runtime]]] [--debug|--release]\n");
        return 2;
    }

    const char  *verb     = argv[1];
    const char  *target   = NULL;
    const char  *config   = "debug";
    const char  *arch     = NULL;
    Mel_Platform platform = host_platform();

    for (int i = 2; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--release") == 0) {
            config = "release";
        } else if (strcmp(a, "--debug") == 0) {
            config = "debug";
        } else if (strncmp(a, "--arch=", 7) == 0) {
            arch = a + 7;
        } else if (strncmp(a, "--", 2) == 0) {
            continue;
        } else if (!target) {
            target = a;
        } else if (!parse_platform(a, &platform)) {
            fprintf(stderr, "nob: unknown platform '%s'\n", a);
            return 2;
        }
    }

    if (strcmp(verb, "build") != 0) {
        fprintf(stderr, "nob: only 'build' is wired so far (got '%s')\n", verb);
        return 2;
    }
    if (!target) {
        fprintf(stderr, "nob: no target given\n");
        return 2;
    }

    Mel_Graph g = {0};
    mel_discover(&g);
    Mel_Variant v = mel_variant_native(platform, config);
    if (arch) v.arch = arch;
    return mel_emit_and_build(&g, target, &v) ? 0 : 1;
}
