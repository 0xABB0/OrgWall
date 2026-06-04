#include "build.h"

void build(Mel_Build* b)
{
    // Backend-agnostic REPL loop: a Mel_Repl_Lang vtable + the loop/session. Pure C,
    // no LLVM, no jit. Any language frontend (C via `clang`, or your own) implements the vtable.
    Mel_Target* lib = mel_add_library(b, "repl");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "string");
    mel_depends(lib, "log");

    Mel_Target* t = mel_add_test(b, "repl-loop");
    mel_sources(t, ALWAYS, "test/repl_test.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "repl");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "string");
}
