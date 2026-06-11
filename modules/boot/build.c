#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "boot");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");

    mel_sources(lib, ALWAYS, "src/boot.c", "src/lifecycle.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS)), "macos/src/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS)), "macos/src/*.m");
    mel_sources(lib, WHEN(.platforms = MEL_ON(IOS)), "ios/src/*.m");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WASM)), "web/src/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(ANDROID)), "android/src/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "win32/src/*.c");

    mel_whole_archive(lib, WHEN(.platforms = MEL_ON(ANDROID)));

    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "AppKit");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(IOS)), "-framework", "UIKit");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(ANDROID)), "-landroid");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "vat");
    mel_depends(lib, "event");
    mel_depends(lib, "executor");
    mel_depends(lib, "log");
    mel_depends(lib, "time");
    mel_depends(lib, "debug");
}
