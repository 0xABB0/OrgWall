#include "build.h"

void build(Mel_Build *b) {
    Mel_Target *lib = mel_add_library(b, "frequency");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/frequency.c");
    mel_depends(lib, "core");
    mel_depends(lib, "math");
}
