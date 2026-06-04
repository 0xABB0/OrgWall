#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "clang");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.cpp");
    mel_cflags(lib, MEL_PRIVATE, ALWAYS, "-fno-rtti");
    mel_depends(lib, "llvm-runtime");
    mel_depends(lib, "llvm");
    mel_depends(lib, "jit");
    mel_depends(lib, "repl");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "string");
    mel_depends(lib, "log");
    Mel_Target* t = mel_add_test(b, "clang-frontend");
    mel_sources(t, ALWAYS, "test/clang_test.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "clang");
    mel_depends(t, "llvm");
    mel_depends(t, "repl");
    mel_depends(t, "jit");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "string");
}
