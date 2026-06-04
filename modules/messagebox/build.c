#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "messagebox");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/messagebox.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "src/apple/*.m");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "src/win32/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(LINUX)), "src/linux/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(ANDROID)), "src/android/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WASM)), "src/web/*.c");

    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "AppKit", "-framework", "Foundation");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(IOS)), "-framework", "UIKit", "-framework", "Foundation");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WIN32)), "-lcomctl32");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(LINUX)), "-ldl");

    mel_android_java(lib, "src/android/java");

    mel_depends(lib, "core");
    mel_depends(lib, "string");
    mel_depends(lib, "color");
    mel_depends(lib, "window");
    mel_depends(lib, "allocator");
    mel_depends(lib, "log");
    mel_depends_when(lib, "platform", WHEN(.platforms = MEL_ON(ANDROID)));

    Mel_Target* t = mel_add_test(b, "messagebox-core");
    mel_includes(t, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(t, ALWAYS, "src/messagebox.c");
    mel_sources(t, ALWAYS, "test/test_messagebox.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "core");
    mel_depends(t, "string");
    mel_depends(t, "color");
    mel_depends(t, "window");
    mel_depends(t, "allocator");
    mel_depends(t, "log");
}
