#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "llvm");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.cpp");
    mel_depends(lib, "jit");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "string");
    mel_depends(lib, "log");
    mel_depends(lib, "collection");
    mel_depends(lib, "llvm-runtime");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(LINUX)), "-lc++");

    Mel_Target* t = mel_add_test(b, "llvm-orc");
    mel_sources(t, ALWAYS, "test/llvm_test.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "llvm");
    mel_depends(t, "jit");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "string");
}
