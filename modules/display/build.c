#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "display");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/display.c", "src/events.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS)), "src/macos/*.m");
    mel_sources(lib, WHEN(.platforms = MEL_ON(LINUX)), "src/linux/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "src/win32/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(ANDROID)), "src/android/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(IOS)), "src/ios/*.m");
    mel_android_java(lib, "src/android/java");

    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "CoreGraphics", "-framework", "IOKit", "-framework", "AppKit", "-framework", "Foundation");

    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "event");
    mel_depends(lib, "executor");
    mel_depends(lib, "log");
    mel_depends(lib, "platform");
    mel_depends(lib, "reflect");

    mel_codegen(lib, "enum-str-gen", "display.enum.gen.c", "$out", "$cflags", "$hostclang", "--", "display/display.h");
    mel_codegen_input(lib, "$dir/include/display/display.h");
    mel_codegen_depfile(lib);

    Mel_Target* t = mel_add_test(b, "display-core");
    mel_sources(t, ALWAYS, "test/display_test.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_link(t, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "AppKit", "-framework", "Foundation");
    mel_depends(t, "test");
    mel_depends(t, "display");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "collection");
    mel_depends(t, "event");
    mel_depends(t, "executor");
    mel_depends(t, "log");
}
