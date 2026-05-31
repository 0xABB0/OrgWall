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

static const char *mf_get(Mel_Target *t, const char *key, const char *dflt) {
    for (size_t i = 0; i < t->manifest.len; i++)
        if (strcmp(t->manifest.items[i].key, key) == 0) return t->manifest.items[i].value;
    return dflt;
}

static int spawn(const char *prog, char **args, int nargs) {
    Mel_StrVec c = {0};
    mel_da_push(&c, (char *)prog);
    for (int i = 0; i < nargs; i++) mel_da_push(&c, args[i]);
    int rc = mel_run_vec(&c);
    free(c.items);
    return rc;
}

static int launch(Mel_Graph *g, const char *target, const Mel_Variant *v, const char *bin,
                  char **xtra, int nxtra) {
    if (v->platform == host_platform()) {
        if (!bin) {
            fprintf(stderr, "nob: '%s' produced nothing to run\n", target);
            return 1;
        }
        fprintf(stderr, "nob: running %s\n", bin);
        return spawn(bin, xtra, nxtra);
    }
    Mel_Target *t   = mel_graph_find(g, target);
    char       *out = t ? mel_target_outdir(t->dir, v) : NULL;
    if (v->platform == MEL_PLATFORM_ANDROID && out) {
        char *apk = mel_str_fmt("%s/android/app/build/outputs/apk/melody/debug/app-melody-debug.apk", out);
        char *act = mel_str_fmt("%s/orgwall.melody.platform.MelodyActivity",
                                mf_get(t, "BUNDLE_ID", t->name));
        char *iargs[] = {"install", "-r", apk};
        if (spawn("adb", iargs, 3) != 0) return 1;
        char *sargs[] = {"shell", "am", "start", "-n", act};
        return spawn("adb", sargs, 5);
    }
    if (v->platform == MEL_PLATFORM_WASM && out) {
        char *dir   = mel_str_fmt("%s", out);
        char *sargs[] = {"modules/build/web/serve.py", dir};
        fprintf(stderr, "nob: serving %s\n", dir);
        return spawn("python3", sargs, 2);
    }
    fprintf(stderr, "nob: cannot run a %s binary on this host\n", mel_platform_name(v->platform));
    return 1;
}

static int debug(Mel_Graph *g, const char *target, const Mel_Variant *v, const char *bin) {
    if (v->platform == MEL_PLATFORM_ANDROID) {
        char *args[] = {"logcat"};
        return spawn("adb", args, 1);
    }
    if (v->platform == host_platform() && bin) {
        char *args[] = {"--", (char *)bin};
        return spawn("lldb", args, 2);
    }
    (void)g;
    fprintf(stderr, "nob: cannot debug a %s binary on this host\n", mel_platform_name(v->platform));
    return 1;
}

static int run_tests(Mel_Graph *g, const char *only, const Mel_Variant *v, char **xtra, int nxtra) {
    int built = 0, failed = 0;
    for (size_t i = 0; i < g->nodes.len; i++) {
        Mel_Target *t = g->nodes.items[i].t;
        if (!t->is_test) continue;
        if (only && strcmp(only, t->name) != 0) continue;
        built++;
        char *bin = NULL;
        if (!mel_emit_and_build(g, t->name, v, true, false, &bin) || !bin) {
            fprintf(stderr, "nob: test '%s' failed to build\n", t->name);
            failed++;
            continue;
        }
        fprintf(stderr, "nob: running test %s\n", t->name);
        if (spawn(bin, xtra, nxtra) != 0) {
            fprintf(stderr, "nob: test '%s' FAILED\n", t->name);
            failed++;
        }
    }
    if (built == 0) {
        fprintf(stderr, "nob: no test targets%s%s found\n", only ? " named " : "", only ? only : "");
        return 1;
    }
    fprintf(stderr, "nob: %d test(s), %d failed\n", built, failed);
    return failed ? 1 : 0;
}

int mel_build_main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
                "usage: nob <build|run|debug|test|configure|compile|link|package|compdb> "
                "<target> [platform[:backend[:runtime]]] [--debug|--release] [--arch=A] [-- args]\n");
        return 2;
    }

    const char  *verb     = argv[1];
    const char  *target   = NULL;
    const char  *config   = "debug";
    const char  *arch     = NULL;
    Mel_Platform platform = host_platform();
    char       **xtra     = NULL;
    int          nxtra    = 0;

    for (int i = 2; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--") == 0) {
            xtra  = &argv[i + 1];
            nxtra = argc - i - 1;
            break;
        } else if (strcmp(a, "--release") == 0) {
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

    Mel_Graph g = {0};
    mel_discover(&g);

    if (strcmp(verb, "compdb") == 0) {
        struct {
            Mel_Variant *items;
            size_t       len, cap;
        } vars = {0};
        for (int i = 2; i < argc; i++) {
            if (strncmp(argv[i], "--", 2) == 0) continue;
            Mel_Platform p;
            if (parse_platform(argv[i], &p)) mel_da_push(&vars, mel_variant_native(p, config));
        }
        if (vars.len == 0) {
            mel_da_push(&vars, mel_variant_native(host_platform(), config));
            for (Mel_Platform p = 0; p < MEL_PLATFORM_COUNT; p++)
                if (p != host_platform()) mel_da_push(&vars, mel_variant_native(p, config));
        }
        bool ok = mel_emit_compdb(&g, vars.items, vars.len, "compile_commands.json");
        if (ok)
            fprintf(stderr, "build: wrote compile_commands.json (%zu platform%s)\n", vars.len,
                    vars.len == 1 ? "" : "s");
        free(vars.items);
        return ok ? 0 : 1;
    }

    Mel_Variant v = mel_variant_native(platform, config);
    if (arch) v.arch = arch;

    if (strcmp(verb, "test") == 0) return run_tests(&g, target, &v, xtra, nxtra);

    if (!target) {
        fprintf(stderr, "nob: no target given\n");
        return 2;
    }

    bool run_ninja = strcmp(verb, "configure") != 0;
    bool do_pkg    = strcmp(verb, "build") == 0 || strcmp(verb, "package") == 0 ||
                  strcmp(verb, "run") == 0;
    bool want_bin = strcmp(verb, "run") == 0 || strcmp(verb, "debug") == 0;

    char *bin = NULL;
    if (!mel_emit_and_build(&g, target, &v, run_ninja, do_pkg, want_bin ? &bin : NULL)) return 1;

    if (strcmp(verb, "run") == 0) return launch(&g, target, &v, bin, xtra, nxtra);
    if (strcmp(verb, "debug") == 0) return debug(&g, target, &v, bin);
    if (strcmp(verb, "build") == 0 || strcmp(verb, "package") == 0 || strcmp(verb, "compile") == 0 ||
        strcmp(verb, "link") == 0 || strcmp(verb, "configure") == 0)
        return 0;

    fprintf(stderr, "nob: unknown verb '%s'\n", verb);
    return 2;
}
