#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "notation");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_includes(lib, MEL_PRIVATE, ALWAYS, "include/notation");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_depends(lib, "core");
    mel_depends(lib, "math");
    mel_depends(lib, "frequency");
    mel_depends(lib, "tuning");
    mel_depends(lib, "musictheory");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "string");
    mel_depends(lib, "coro");
    mel_codegen(lib, "coro-gen", "identify.gen.c", "$dir/include/notation/identify.coro.h", "$out", "-DMEL_CORO_CODEGEN", "$cflags", "$hostclang");
    mel_codegen_input(lib, "$dir/include/notation/identify.coro.h");
    mel_codegen_depfile(lib);

    Mel_Target* t = mel_add_test(b, "notation-test");
    mel_sources(t, ALWAYS, "test/test_notation.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "notation");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
}
