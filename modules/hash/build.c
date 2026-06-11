#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "hash");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_depends(lib, "core");

    Mel_Target* test = mel_add_test(b, "hash-vectors");
    mel_sources(test, ALWAYS, "test/test_hash.c");
    mel_sources(test, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(test, "test");
    mel_depends(test, "hash");
    mel_depends(test, "core");
}
