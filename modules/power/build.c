#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "power");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/power.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS)), "macos/src/*.m", "apple/src/*.m");
    mel_sources(lib, WHEN(.platforms = MEL_ON(IOS)), "ios/src/*.m", "apple/src/*.m");
    mel_sources(lib, WHEN(.platforms = MEL_ON(LINUX)), "linux/src/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "win32/src/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(ANDROID)), "android/src/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WASM)), "web/src/*.c");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "-framework", "Foundation");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "IOKit");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(IOS)), "-framework", "UIKit");
    mel_depends(lib, "core");
    mel_depends(lib, "platform");
}
