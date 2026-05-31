#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "temperature");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_depends(lib, "core");
    mel_depends(lib, "math");

    Mel_Target* ex = mel_add_executable(b, "temperature-example");
    mel_sources(ex, ALWAYS, "example/temperature_example.c");
    mel_depends(ex, "temperature");
    mel_depends(ex, "core");
}
