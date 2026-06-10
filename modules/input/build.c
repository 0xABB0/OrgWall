#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "input");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/input.c", "src/events.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS)), "src/macos/*.m");
    mel_sources(lib, WHEN(.platforms = MEL_ON(IOS)), "src/ios/*.m");
    mel_sources(lib, WHEN(.platforms = MEL_ON(LINUX)), "src/linux/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "src/win32/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(ANDROID)), "src/android/*.c");
    mel_android_java(lib, "src/android/java");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WASM)), "src/wasm/*.c");

    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "AppKit", "-framework", "CoreGraphics", "-framework", "Carbon", "-framework", "Foundation");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(IOS)), "-framework", "UIKit", "-framework", "Foundation");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WIN32)), "-luser32", "-lgdi32", "-limm32");

    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "event");
    mel_depends(lib, "executor");
    mel_depends(lib, "log");
    mel_depends(lib, "string");
    mel_depends(lib, "reflect");
    mel_depends(lib, "platform");

    mel_codegen(lib, "enum-str-gen", "input.enum.gen.c", "$out", "$cflags", "$hostclang", "--", "input/scancode.h");
    mel_codegen_input(lib, "$dir/include/input/scancode.h");
    mel_codegen_depfile(lib);

    Mel_Target* t = mel_add_test(b, "input-core");
    mel_sources(t, ALWAYS, "test/input_test.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_link(t, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "AppKit", "-framework", "CoreGraphics", "-framework", "Carbon", "-framework", "Foundation");
    mel_depends(t, "test");
    mel_depends(t, "input");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "collection");
    mel_depends(t, "event");
    mel_depends(t, "executor");
    mel_depends(t, "log");
    mel_depends(t, "string");
}
