#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "musictheory");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_includes(lib, MEL_PRIVATE, ALWAYS, "include/musictheory");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_depends(lib, "core");
    mel_depends(lib, "math");
    mel_depends(lib, "frequency");
    mel_depends(lib, "musictuning");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "coro");
    mel_codegen(lib, "coro-gen", "scale_gen.gen.c", "$dir/include/musictheory/scale_gen.coro.h", "$out", "-DMEL_CORO_CODEGEN", "$cflags", "$hostclang");
    mel_codegen_input(lib, "$dir/include/musictheory/scale_gen.coro.h");
    mel_codegen_depfile(lib);

    Mel_Target* t = mel_add_test(b, "musictheory-test");
    mel_sources(t, ALWAYS, "test/test_musictheory.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "musictheory");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
}
