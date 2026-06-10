#include "build.h"

#include <stdio.h>
#include <stdlib.h>

static char* fmt(const char* f, const char* a)
{
    char* s = malloc(256);
    snprintf(s, 256, f, a);
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
}
