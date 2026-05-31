#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "easing");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_depends(lib, "math");
}
