#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "gui");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_sources(lib, WHEN(.backend = "cocoa"), "src/cocoa/*.m");
    mel_sources(lib, WHEN(.backend = "uikit"), "src/uikit/*.m");
    mel_sources(lib, WHEN(.backend = "dom"), "src/dom/*.c");
    mel_sources(lib, WHEN(.backend = "winui"), "src/winui/*.c");
    mel_sources(lib, WHEN(.backend = "androidnative"), "src/androidnative/*.c");
    mel_android_namespace(lib, "orgwall.melody.platform");
    mel_android_java(lib, "src/androidnative/java");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "Cocoa", "-framework", "QuartzCore", "-framework", "CoreGraphics");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(IOS)), "-framework", "UIKit", "-framework", "QuartzCore", "-framework", "CoreGraphics");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WIN32)), "-lcomctl32", "-lgdi32");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(ANDROID)), "-landroid");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "math");
    mel_depends(lib, "platform");
    mel_depends(lib, "reactor");
    mel_depends(lib, "string");
    mel_depends(lib, "window");
    mel_depends(lib, "color");
    mel_depends(lib, "paint");
}
