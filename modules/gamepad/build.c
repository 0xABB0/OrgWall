#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "gamepad");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/joystick.c", "src/gamepad.c", "src/virtual.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS)), "macos/src/*.m");
    mel_includes(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "macos/include");
    mel_sources(lib, WHEN(.platforms = MEL_ON(IOS)), "ios/src/*.m");
    mel_includes(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(IOS)), "ios/include");
    mel_sources(lib, WHEN(.platforms = MEL_ON(LINUX)), "linux/src/*.c");
    mel_includes(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(LINUX)), "linux/include");
    mel_sources(lib, WHEN(.platforms = MEL_ON(ANDROID)), "android/src/*.c");
    mel_includes(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(ANDROID)), "android/include");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "win32/src/*.c");
    mel_includes(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WIN32)), "win32/include");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WASM)), "wasm/src/*.c");
    mel_includes(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WASM)), "wasm/include");

    mel_android_java(lib, "android/java");

    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "GameController", "-framework", "IOKit", "-framework", "Foundation");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(IOS)), "-framework", "GameController", "-framework", "Foundation");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WIN32)), "-lxinput");

    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "string");
    mel_depends(lib, "event");
    mel_depends(lib, "executor");
    mel_depends(lib, "log");
    mel_depends(lib, "reflect");
    mel_depends(lib, "guid");
    mel_depends(lib, "platform");
    mel_depends(lib, "thread");
    mel_depends(lib, "debug");

    mel_codegen(lib, "enum-str-gen", "gamepad.protocol.enum.gen.c", "$out", "$cflags", "$hostclang", "--", "gamepad/protocol.h");
    mel_codegen_input(lib, "$dir/include/gamepad/protocol.h");
    mel_codegen_depfile(lib);
    mel_codegen(lib, "enum-str-gen", "gamepad.gamepad.enum.gen.c", "$out", "$cflags", "$hostclang", "--", "gamepad/gamepad.h");
    mel_codegen_input(lib, "$dir/include/gamepad/gamepad.h");
    mel_codegen_depfile(lib);

    Mel_Target* t = mel_add_test(b, "gamepad-core");
    mel_sources(t, ALWAYS, "test/gamepad_test.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "gamepad");
    mel_depends(t, "guid");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "collection");
    mel_depends(t, "string");
    mel_depends(t, "event");
    mel_depends(t, "executor");
    mel_depends(t, "log");
    mel_depends(t, "thread");
    mel_depends(t, "debug");
}
