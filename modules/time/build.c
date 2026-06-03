#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "time");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/duration.c");
    mel_sources(lib, ALWAYS, "src/clock.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS) | MEL_ON(LINUX) | MEL_ON(ANDROID) | MEL_ON(WASM)), "src/nano.unix.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "src/nano.win32.c");
    mel_depends(lib, "core");
    mel_depends(lib, "string");

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
}
