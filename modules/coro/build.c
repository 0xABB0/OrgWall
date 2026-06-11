#include "build.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* fmt(const char* f, const char* a)
{
    char* s = malloc(256);
    snprintf(s, 256, f, a);
    return s;
}

static char* dfmt(const char* name, const char* val)
{
    size_t n = strlen(name) + strlen(val) + 4;
    char*  s = malloc(n);
    snprintf(s, n, "-D%s=%s", name, val);
    return s;
}

static char* join(const char* a, const char* b)
{
    size_t la = strlen(a), lb = strlen(b);
    char*  s = malloc(la + lb + 2);
    memcpy(s, a, la);
    s[la] = '/';
    memcpy(s + la + 1, b, lb + 1);
    return s;
}

static char* module_dir(void)
{
    static const char* f = __FILE__;
    char*              s = malloc(strlen(f) + 1);
    strcpy(s, f);
    char* sl = strrchr(s, '/');
    if (sl)
        *sl = 0;
    return s;
}

static void coro_test(Mel_Build* b, const char* name)
{
    Mel_Target* t = mel_add_test(b, fmt("coro-test-%s", name));
    mel_sources(t, ALWAYS, fmt("test/driver/%s_diff.c", name));
    mel_includes(t, MEL_PRIVATE, ALWAYS, "test/driver", "test/fixtures", "include");
    mel_depends(t, "coro");
    mel_depends(t, "core");
    mel_codegen(t, "coro-gen", fmt("%s.gen.c", name), fmt("$dir/test/fixtures/%s.coro.h", name), "$out", "-DMEL_CORO_CODEGEN", "$cflags", "$hostclang");
    mel_codegen_input(t, fmt("$dir/test/fixtures/%s.coro.h", name));
    mel_codegen_depfile(t);
}

static void coro_snapshot_test(Mel_Build* b)
{
    Mel_Target* t = mel_add_test(b, "coro-test-snapshot");
    mel_sources(t, ALWAYS, "test/driver/snapshot.c");
    mel_includes(t, MEL_PRIVATE, ALWAYS, "test/driver", "test/fixtures", "include");
    mel_depends(t, "coro");
    mel_depends(t, "core");
    mel_codegen(t, "coro-gen", "sum_to_snap.gen.c", "$dir/test/fixtures/sum_to.coro.h", "$out", "-DMEL_CORO_CODEGEN", "$cflags", "$hostclang");
    mel_codegen_input(t, "$dir/test/fixtures/sum_to.coro.h");
    mel_codegen_depfile(t);
}

static void coro_golden_test(Mel_Build* b, const char* name, const char* mdir, const char* icore)
{
    Mel_Target* t = mel_add_test(b, fmt("coro-test-golden-%s", name));
    mel_sources(t, ALWAYS, "test/driver/golden.c");
    mel_cflags(t, MEL_PRIVATE, ALWAYS, dfmt("FIXTURE_NAME", name), dfmt("CORO_GEN", join(mdir, "build/host/coro-gen")), dfmt("CORO_MODULE_DIR", mdir), dfmt("CORE_INCLUDE_DIR", icore));
    mel_depends_host(t, "coro-gen");
}

static void coro_reject_test(Mel_Build* b, const char* name, const char* mdir, const char* icore)
{
    Mel_Target* t = mel_add_test(b, fmt("coro-test-reject-%s", name));
    mel_sources(t, ALWAYS, "test/driver/reject.c");
    mel_cflags(t, MEL_PRIVATE, ALWAYS, dfmt("FIXTURE_NAME", name), dfmt("CORO_GEN", join(mdir, "build/host/coro-gen")), dfmt("CORO_MODULE_DIR", mdir), dfmt("CORE_INCLUDE_DIR", icore));
    mel_depends_host(t, "coro-gen");
}

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "coro");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_depends(lib, "core");

    Mel_Target* gen = mel_add_host_tool(b, "coro-gen");
    mel_sources(gen, ALWAYS, "codegen/coro_gen.c");
    mel_cflags(gen, MEL_PRIVATE, ALWAYS, "-I/opt/homebrew/opt/llvm/include");
    mel_link(gen, MEL_PRIVATE, ALWAYS, "-L/opt/homebrew/opt/llvm/lib", "-lclang", "-Wl,-rpath,/opt/homebrew/opt/llvm/lib");

    Mel_Target* ex = mel_add_executable(b, "coro-example");
    mel_sources(ex, ALWAYS, "example/app.c");
    mel_includes(ex, MEL_PRIVATE, ALWAYS, "example");
    mel_depends(ex, "coro");
    mel_depends(ex, "core");
    mel_codegen(ex, "coro-gen", "ticker.gen.c", "$dir/example/ticker.coro.h", "$out", "-DMEL_CORO_CODEGEN", "$cflags", "$hostclang");
    mel_codegen_input(ex, "$dir/example/ticker.coro.h");
    mel_codegen_depfile(ex);

    static const char* fixtures[] = { "sum_to", "countdown", "classify", "relay", "repeat_sum" };
    for (int i = 0; i < 5; i++)
        coro_test(b, fixtures[i]);

    coro_snapshot_test(b);

    char* mdir = module_dir();
    char* icore = join(join(mdir, ".."), "core/include");

    for (int i = 0; i < 5; i++)
        coro_golden_test(b, fixtures[i], mdir, icore);

    static const char* rejects[] = { "addr_of_lifted", "goto_cross", "switch_cross", "vla_lifted" };
    for (int i = 0; i < 4; i++)
        coro_reject_test(b, rejects[i], mdir, icore);
}
