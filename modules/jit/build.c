#include "build.h"

void build(Mel_Build* b)
{
    // Backend-agnostic JIT interface: a Mel_Jit_Backend vtable + a dispatching facade.
    // Pure C, no LLVM. Concrete backends (e.g. the `llvm` ORC backend) plug in at runtime.
    Mel_Target* lib = mel_add_library(b, "jit");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "log");

    Mel_Target* t = mel_add_test(b, "jit-facade");
    mel_sources(t, ALWAYS, "test/jit_test.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "jit");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
}
