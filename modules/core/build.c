#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "core");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
}
