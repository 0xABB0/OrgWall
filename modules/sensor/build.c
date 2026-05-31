#include "build.h"

void build(Mel_Build *b) {
    Mel_Target *lib = mel_add_library(b, "sensor");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS)), "src/macos/*.c", "src/macos/*.m",
                "src/apple/*.c", "src/apple/*.m");
    mel_sources(lib, WHEN(.platforms = MEL_ON(IOS)), "src/ios/*.m", "src/apple/*.c", "src/apple/*.m");
    mel_sources(lib, WHEN(.platforms = MEL_ON(LINUX)), "src/linux/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "src/win32/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(ANDROID)), "src/android/*.c");
    mel_depends(lib, "core");
    mel_depends(lib, "platform");
}
