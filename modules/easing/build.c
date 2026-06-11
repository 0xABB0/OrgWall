#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "easing");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_depends(lib, "math");

    Mel_Target* t = mel_add_test(b, "easing-test");
    mel_sources(t, ALWAYS, "test/test.easing.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "easing");
    mel_depends(t, "core");
}
