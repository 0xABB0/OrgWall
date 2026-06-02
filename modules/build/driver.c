#include "runner.h"

#include <stdio.h>

static char* sim_capture(const char* cmd)
{
    FILE* p = popen(cmd, "r");
    if (!p)
        return mel_str_dup("");
    char*  buf = malloc(1 << 12);
    size_t k = fread(buf, 1, (1 << 12) - 1, p);
    pclose(p);
    while (k && (buf[k - 1] == '\n' || buf[k - 1] == ' '))
        k--;
    buf[k] = 0;
    return buf;
}

static Mel_Platform host_platform(void)
{
#if defined(__APPLE__)
    return MEL_PLATFORM_MACOS;
#elif defined(_WIN32)
    return MEL_PLATFORM_WIN32;
#else
    return MEL_PLATFORM_LINUX;
#endif
}

static bool tok_is(const char* tok, size_t n, const char* name) { return strlen(name) == n && strncmp(tok, name, n) == 0; }

static bool parse_platform(const char* tok, Mel_Platform* out)
{
    size_t n = strcspn(tok, ":");
    if (tok_is(tok, n, "macos"))
        return *out = MEL_PLATFORM_MACOS, true;
    if (tok_is(tok, n, "ios"))
        return *out = MEL_PLATFORM_IOS, true;
    if (tok_is(tok, n, "linux"))
        return *out = MEL_PLATFORM_LINUX, true;
    if (tok_is(tok, n, "android"))
        return *out = MEL_PLATFORM_ANDROID, true;
    if (tok_is(tok, n, "win32"))
        return *out = MEL_PLATFORM_WIN32, true;
    if (tok_is(tok, n, "wasm"))
        return *out = MEL_PLATFORM_WASM, true;
    return false;
}

static bool gpu_valid(Mel_Platform p, const char* g)
{
    switch (p)
    {
    case MEL_PLATFORM_MACOS:
        return !strcmp(g, "metal") || !strcmp(g, "vulkan") || !strcmp(g, "webgpu");
    case MEL_PLATFORM_IOS:
        return !strcmp(g, "metal");
    case MEL_PLATFORM_LINUX:
        return !strcmp(g, "vulkan");
    case MEL_PLATFORM_ANDROID:
        return !strcmp(g, "vulkan") || !strcmp(g, "webgpu");
    case MEL_PLATFORM_WIN32:
        return !strcmp(g, "vulkan") || !strcmp(g, "d3d12");
    case MEL_PLATFORM_WASM:
        return !strcmp(g, "webgpu");
    default:
        return false;
    }
}

static const char* mf_get(Mel_Target* t, const char* key, const char* dflt)
{
    for (size_t i = 0; i < t->manifest.len; i++)
        if (strcmp(t->manifest.items[i].key, key) == 0)
            return t->manifest.items[i].value;
    return dflt;
}

static int spawn(const char* prog, char** args, int nargs)
{
    Mel_StrVec c = { 0 };
    mel_da_push(&c, (char*)prog);
    for (int i = 0; i < nargs; i++)
        mel_da_push(&c, args[i]);
    int rc = mel_run_vec(&c);
    free(c.items);
    return rc;
}

static char* android_emulator_bin(void)
{
    const char* sdk = getenv("ANDROID_HOME");
    if (!sdk)
        sdk = getenv("ANDROID_SDK_ROOT");
    char* root = sdk ? mel_str_dup(sdk) : mel_str_fmt("%s/Library/Android/sdk", getenv("HOME"));
    char* bin = mel_str_fmt("%s/emulator/emulator", root);
    free(root);
    if (mel_path_is_file(bin))
        return bin;
    free(bin);
    return mel_str_dup("emulator");
}

static char* android_emulator_serial(void)
{
    return sim_capture("adb devices | awk '/^emulator-/ && $2 == \"device\" { print $1; exit }'");
}

static char* android_boot_emu(const char* avd)
{
    char* serial = android_emulator_serial();
    if (*serial)
    {
        fprintf(stderr, "nob: emulator %s already running\n", serial);
        return serial;
    }
    free(serial);

    char* emu = android_emulator_bin();
    char* name;
    if (avd)
        name = mel_str_dup((char*)avd);
    else
    {
        char* lc = mel_str_fmt("%s -list-avds | head -1", emu);
        name = sim_capture(lc);
        free(lc);
    }
    if (!*name)
    {
        fprintf(stderr, "nob: no Android AVD found; create one with avdmanager\n");
        free(emu);
        free(name);
        return NULL;
    }

    fprintf(stderr, "nob: booting emulator %s\n", name);
    char* cmd = mel_str_fmt("%s -avd %s >/dev/null 2>&1 & adb wait-for-device shell "
                            "'while [ \"$(getprop sys.boot_completed)\" != 1 ]; do sleep 1; done'",
                            emu,
                            name);
    system(cmd);
    free(cmd);
    free(emu);
    free(name);

    serial = android_emulator_serial();
    if (!*serial)
    {
        fprintf(stderr, "nob: emulator failed to come online\n");
        free(serial);
        return NULL;
    }
    fprintf(stderr, "nob: emulator %s ready\n", serial);
    return serial;
}

static int launch_ios_sim(Mel_Target* t, const char* out)
{
    char* udid = sim_capture("xcrun simctl list devices booted | grep -Eo "
                             "'[0-9A-Fa-f-]{36}' | head -1");
    if (!*udid)
    {
        free(udid);
        udid = sim_capture("xcrun simctl list devices available | grep -E 'iPhone ' | "
                           "grep -Eo '[0-9A-Fa-f-]{36}' | head -1");
        if (!*udid)
        {
            fprintf(stderr, "nob: no iOS simulator available\n");
            free(udid);
            return 1;
        }
    }
    char* app = mel_str_fmt("%s/%s.app", out, t->name);
    char* bundle = (char*)mf_get(t, "BUNDLE_ID", t->name);

    char* open_args[] = { "-a", "Simulator" };
    spawn("open", open_args, 2);
    char* boot_args[] = { "simctl", "bootstatus", udid, "-b" };
    if (spawn("xcrun", boot_args, 4) != 0)
    {
        free(udid);
        free(app);
        return 1;
    }
    char* inst_args[] = { "simctl", "install", udid, app };
    if (spawn("xcrun", inst_args, 4) != 0)
    {
        free(udid);
        free(app);
        return 1;
    }
    fprintf(stderr, "nob: launching %s on simulator %s\n", bundle, udid);
    char* run_args[] = { "simctl", "launch", "--console-pty", "--terminate-running-process", udid, bundle };
    int   rc = spawn("xcrun", run_args, 6);
    free(udid);
    free(app);
    return rc;
}

static int launch(Mel_Graph* g, const char* target, const Mel_Variant* v, const char* bin, char** xtra, int nxtra)
{
    if (v->platform == host_platform())
    {
        if (!bin)
        {
            fprintf(stderr, "nob: '%s' produced nothing to run\n", target);
            return 1;
        }
        fprintf(stderr, "nob: running %s\n", bin);
        return spawn(bin, xtra, nxtra);
    }
    Mel_Target* t = mel_graph_find(g, target);
    char*       out = t ? mel_target_outdir(t->dir, v) : NULL;
    if (v->platform == MEL_PLATFORM_IOS && v->simulator && t && out)
        return launch_ios_sim(t, out);
    if (v->platform == MEL_PLATFORM_ANDROID && out)
    {
        char* serial = NULL;
        if (v->simulator)
        {
            serial = android_boot_emu(mf_get(t, "AVD", NULL));
            if (!serial)
                return 1;
        }
        char* apk = mel_str_fmt("%s/android/app/build/outputs/apk/melody/debug/app-melody-debug.apk", out);
        char* act = mel_str_fmt("%s/orgwall.melody.platform.MelodyActivity", mf_get(t, "BUNDLE_ID", t->name));
        if (serial && *serial)
        {
            char* iargs[] = { "-s", serial, "install", "-r", apk };
            if (spawn("adb", iargs, 5) != 0)
                return 1;
            char* sargs[] = { "-s", serial, "shell", "am", "start", "-n", act };
            return spawn("adb", sargs, 7);
        }
        char* iargs[] = { "install", "-r", apk };
        if (spawn("adb", iargs, 3) != 0)
            return 1;
        char* sargs[] = { "shell", "am", "start", "-n", act };
        return spawn("adb", sargs, 5);
    }
    if (v->platform == MEL_PLATFORM_WASM && out)
    {
        char* dir = mel_str_fmt("%s", out);
        char* sargs[] = { "modules/build/web/serve.py", dir };
        fprintf(stderr, "nob: serving %s\n", dir);
        return spawn("python3", sargs, 2);
    }
    fprintf(stderr, "nob: cannot run a %s binary on this host\n", mel_platform_name(v->platform));
    return 1;
}

static int debug(Mel_Graph* g, const char* target, const Mel_Variant* v, const char* bin)
{
    if (v->platform == MEL_PLATFORM_ANDROID)
    {
        char* args[] = { "logcat" };
        return spawn("adb", args, 1);
    }
    if (v->platform == host_platform() && bin)
    {
        char* args[] = { "--", (char*)bin };
        return spawn("lldb", args, 2);
    }
    (void)g;
    fprintf(stderr, "nob: cannot debug a %s binary on this host\n", mel_platform_name(v->platform));
    return 1;
}

static int run_tests(Mel_Graph* g, const char* only, const Mel_Variant* v, char** xtra, int nxtra)
{
    int built = 0, failed = 0;
    for (size_t i = 0; i < g->nodes.len; i++)
    {
        Mel_Target* t = g->nodes.items[i].t;
        if (!t->is_test)
            continue;
        if (only && strcmp(only, t->name) != 0)
            continue;
        built++;
        char* bin = NULL;
        if (!mel_emit_and_build(g, t->name, v, true, false, &bin) || !bin)
        {
            fprintf(stderr, "nob: test '%s' failed to build\n", t->name);
            failed++;
            continue;
        }
        fprintf(stderr, "nob: running test %s\n", t->name);
        if (spawn(bin, xtra, nxtra) != 0)
        {
            fprintf(stderr, "nob: test '%s' FAILED\n", t->name);
            failed++;
        }
    }
    if (built == 0)
    {
        fprintf(stderr, "nob: no test targets%s%s found\n", only ? " named " : "", only ? only : "");
        return 1;
    }
    fprintf(stderr, "nob: %d test(s), %d failed\n", built, failed);
    return failed ? 1 : 0;
}

int mel_build_main(int argc, char** argv)
{
    if (argc < 2)
    {
        fprintf(stderr,
                "usage: nob <build|run|debug|test|configure|compile|link|package|compdb> "
                "<target> [platform[:backend[:runtime]]] [--debug|--release] [--arch=A] [-- args]\n");
        return 2;
    }

    const char*  verb = argv[1];
    const char*  target = NULL;
    const char*  config = "debug";
    const char*  arch = NULL;
    const char*  gpu = NULL;
    Mel_Platform platform = host_platform();
    bool         device = false;
    char**       xtra = NULL;
    int          nxtra = 0;

    for (int i = 2; i < argc; i++)
    {
        const char* a = argv[i];
        if (strcmp(a, "--") == 0)
        {
            xtra = &argv[i + 1];
            nxtra = argc - i - 1;
            break;
        }
        else if (strcmp(a, "--release") == 0)
        {
            config = "release";
        }
        else if (strcmp(a, "--debug") == 0)
        {
            config = "debug";
        }
        else if (strncmp(a, "--arch=", 7) == 0)
        {
            arch = a + 7;
        }
        else if (strncmp(a, "--gpu=", 6) == 0)
        {
            gpu = a + 6;
        }
        else if (strcmp(a, "--gpu") == 0)
        {
            if (i + 1 < argc)
                gpu = argv[++i];
        }
        else if (strcmp(a, "--device") == 0)
        {
            device = true;
        }
        else if (strncmp(a, "--", 2) == 0)
        {
            continue;
        }
        else if (!target)
        {
            target = a;
        }
        else if (!parse_platform(a, &platform))
        {
            fprintf(stderr, "nob: unknown platform '%s'\n", a);
            return 2;
        }
    }

    Mel_Graph g = { 0 };
    mel_discover(&g);

    if (strcmp(verb, "compdb") == 0)
    {
        struct
        {
            Mel_Variant* items;
            size_t       len, cap;
        } vars = { 0 };
        for (int i = 2; i < argc; i++)
        {
            if (strncmp(argv[i], "--", 2) == 0)
                continue;
            Mel_Platform p;
            if (parse_platform(argv[i], &p))
                mel_da_push(&vars, mel_variant_native(p, config));
        }
        if (vars.len == 0)
        {
            mel_da_push(&vars, mel_variant_native(host_platform(), config));
            for (Mel_Platform p = 0; p < MEL_PLATFORM_COUNT; p++)
                if (p != host_platform())
                    mel_da_push(&vars, mel_variant_native(p, config));
        }
        bool ok = mel_emit_compdb(&g, vars.items, vars.len, "compile_commands.json");
        if (ok)
            fprintf(stderr, "build: wrote compile_commands.json (%zu platform%s)\n", vars.len, vars.len == 1 ? "" : "s");
        free(vars.items);
        return ok ? 0 : 1;
    }

    Mel_Variant v = mel_variant_native(platform, config);
    if (device)
        v.simulator = false;
    if (arch)
        v.arch = arch;
    if (gpu)
    {
        if (!gpu_valid(platform, gpu))
        {
            fprintf(stderr, "nob: gpu backend '%s' is not valid for %s\n", gpu, mel_platform_name(platform));
            return 2;
        }
        v.gpu = gpu;
    }

    if (strcmp(verb, "test") == 0)
        return run_tests(&g, target, &v, xtra, nxtra);

    if (!target)
    {
        fprintf(stderr, "nob: no target given\n");
        return 2;
    }

    bool run_ninja = strcmp(verb, "configure") != 0;
    bool do_pkg = strcmp(verb, "build") == 0 || strcmp(verb, "package") == 0 || strcmp(verb, "run") == 0;
    bool want_bin = strcmp(verb, "run") == 0 || strcmp(verb, "debug") == 0;

    char* bin = NULL;
    if (!mel_emit_and_build(&g, target, &v, run_ninja, do_pkg, want_bin ? &bin : NULL))
        return 1;

    if (strcmp(verb, "run") == 0)
        return launch(&g, target, &v, bin, xtra, nxtra);
    if (strcmp(verb, "debug") == 0)
        return debug(&g, target, &v, bin);
    if (strcmp(verb, "build") == 0 || strcmp(verb, "package") == 0 || strcmp(verb, "compile") == 0 || strcmp(verb, "link") == 0 || strcmp(verb, "configure") == 0)
        return 0;

    fprintf(stderr, "nob: unknown verb '%s'\n", verb);
    return 2;
}
