#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "time");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/duration.c");
    mel_sources(lib, ALWAYS, "src/clock.c");
    mel_sources(lib, ALWAYS, "src/sleep.c");
    mel_sources(lib, ALWAYS, "src/format_prefs.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS) | MEL_ON(LINUX) | MEL_ON(ANDROID) | MEL_ON(WASM)), "src/nano.unix.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS) | MEL_ON(LINUX) | MEL_ON(ANDROID) | MEL_ON(WASM)), "src/sleep.unix.c");
    mel_cflags(lib, MEL_PRIVATE, WHEN(.platforms = MEL_ON(WASM)), "-D_POSIX_C_SOURCE=199309L");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "src/nano.win32.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "src/sleep.win32.c");

    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "apple/src/*.m");
    mel_sources(lib, WHEN(.platforms = MEL_ON(LINUX)), "linux/src/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "win32/src/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(ANDROID) | MEL_ON(WASM)), "none/src/*.c");

    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "-framework", "Foundation");

    mel_depends(lib, "core");
    mel_depends(lib, "string");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "debug");

    Mel_Target* t = mel_add_test(b, "time-duration");
    mel_sources(t, ALWAYS, "test/test.duration.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "time");
    mel_depends(t, "test");
    mel_depends(t, "core");
    mel_depends(t, "string");
    mel_depends(t, "allocator");

    Mel_Target* tf = mel_add_test(b, "time-frame-clock");
    mel_sources(tf, ALWAYS, "test/test.frame_clock.c");
    mel_sources(tf, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(tf, "time");
    mel_depends(tf, "test");
    mel_depends(tf, "core");

    Mel_Target* tc = mel_add_test(b, "time-clock");
    mel_sources(tc, ALWAYS, "test/test.clock.c");
    mel_sources(tc, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(tc, "time");
    mel_depends(tc, "test");
    mel_depends(tc, "core");
    mel_depends(tc, "string");
    mel_depends(tc, "allocator");

    Mel_Target* ts = mel_add_test(b, "time-sleep");
    mel_sources(ts, ALWAYS, "test/test.sleep.c");
    mel_sources(ts, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(ts, "time");
    mel_depends(ts, "test");
    mel_depends(ts, "core");

    Mel_Target* tp = mel_add_test(b, "time-format-prefs");
    mel_sources(tp, ALWAYS, "test/test.format_prefs.c");
    mel_sources(tp, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(tp, "time");
    mel_depends(tp, "test");
    mel_depends(tp, "core");
    mel_depends(tp, "allocator");
    mel_depends(tp, "collection");
    mel_depends(tp, "debug");
}
