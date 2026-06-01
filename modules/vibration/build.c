#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "vibration");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/vibration.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(ANDROID)), "src/android/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "src/apple/*.m");
    mel_sources(lib, WHEN(.platforms = MEL_ON(LINUX) | MEL_ON(WIN32) | MEL_ON(WASM)), "src/vibration_host_none.c");
    mel_android_manifest(lib, "src/android/AndroidManifest.xml");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "-framework", "CoreHaptics", "-framework", "Foundation");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "string");
    mel_depends(lib, "reactor");
    mel_depends(lib, "time");
    mel_depends(lib, "log");
    mel_depends(lib, "platform");
}
