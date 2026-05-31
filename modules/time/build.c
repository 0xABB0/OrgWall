#include "build.h"

void build(Mel_Build *b) {
    Mel_Target *lib = mel_add_library(b, "time");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/frequency.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS) | MEL_ON(LINUX) | MEL_ON(ANDROID)),
                "src/nano.unix.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "src/nano.win32.c");
    mel_depends(lib, "core");
    mel_depends(lib, "math");
}
