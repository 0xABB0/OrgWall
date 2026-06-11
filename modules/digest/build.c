#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "digest");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_depends(lib, "core");

    Mel_Target* test = mel_add_test(b, "digest-vectors");
    mel_sources(test, ALWAYS, "test/test_digest.c");
    mel_sources(test, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(test, "test");
    mel_depends(test, "digest");
    mel_depends(test, "core");
}
