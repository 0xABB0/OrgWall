#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "thermal");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/sensor.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS)), "src/macos/*.c", "src/apple/*.c", "src/apple/*.m");
    mel_sources(lib, WHEN(.platforms = MEL_ON(IOS)), "src/ios/*.c", "src/apple/*.c", "src/apple/*.m");
    mel_sources(lib, WHEN(.platforms = MEL_ON(LINUX)), "src/linux/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "src/win32/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(ANDROID)), "src/android/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WASM)), "src/web/*.c");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "-framework", "Foundation");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "IOKit");
    mel_depends(lib, "core");
    mel_depends(lib, "platform");
    mel_depends(lib, "temperature");
    mel_depends(lib, "allocator");

    Mel_Target* ex = mel_add_executable(b, "thermal-sensors");
    mel_sources(ex, ALWAYS, "example/thermal_sensors.c");
    mel_depends(ex, "thermal");
    mel_depends(ex, "temperature");
    mel_depends(ex, "allocator");
    mel_depends(ex, "core");
}
