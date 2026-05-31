#include "build.h"

void build(Mel_Build *b) {
    Mel_Target *lib = mel_add_library(b, "mongoose");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, ".");
    mel_sources(lib, ALWAYS, "mongoose.c");
}
