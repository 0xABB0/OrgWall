#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "audio");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");

    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "src/coreaudio/*.m");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "-framework", "AudioToolbox", "-framework", "CoreAudio", "-framework", "AudioUnit");

    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "src/wasapi/*.c");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WIN32)), "-lole32", "-lksuser");

    mel_sources(lib, WHEN(.platforms = MEL_ON(LINUX)), "src/alsa/*.c");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(LINUX)), "-lasound");

    mel_sources(lib, WHEN(.platforms = MEL_ON(ANDROID)), "src/aaudio/*.c");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(ANDROID)), "-laaudio");

    mel_sources(lib, WHEN(.platforms = MEL_ON(WASM)), "src/web/*.c");

    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "math");
    mel_depends(lib, "thread");
    mel_depends(lib, "time");
    mel_depends(lib, "reactor");
    mel_depends(lib, "executor");
    mel_depends(lib, "future");
    mel_depends(lib, "event");
    mel_depends(lib, "channel");
    mel_depends(lib, "log");
    mel_depends(lib, "debug");
    mel_depends(lib, "string");

    Mel_Target* voice = mel_add_test(b, "audio-voice");
    mel_sources(voice, ALWAYS, "test/test_voice_handle.c");
    mel_sources(voice, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(voice, "test");
    mel_depends(voice, "audio");
    mel_depends(voice, "core");
    mel_depends(voice, "allocator");
    mel_depends(voice, "collection");

    Mel_Target* render = mel_add_test(b, "audio-render");
    mel_sources(render, ALWAYS, "test/test_render_offline.c");
    mel_sources(render, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(render, "test");
    mel_depends(render, "audio");
    mel_depends(render, "core");
    mel_depends(render, "allocator");
    mel_depends(render, "collection");

    Mel_Target* fader = mel_add_test(b, "audio-fader");
    mel_sources(fader, ALWAYS, "test/test_fader.c");
    mel_sources(fader, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(fader, "test");
    mel_depends(fader, "audio");
    mel_depends(fader, "core");
    mel_depends(fader, "allocator");
    mel_depends(fader, "collection");

    Mel_Target* ring = mel_add_test(b, "audio-ring");
    mel_sources(ring, ALWAYS, "test/test_ring_spsc.c");
    mel_sources(ring, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(ring, "test");
    mel_depends(ring, "audio");
    mel_depends(ring, "core");
    mel_depends(ring, "allocator");
    mel_depends(ring, "collection");

    Mel_Target* online = mel_add_test(b, "audio-online");
    mel_sources(online, ALWAYS, "test/test_online_stress.c");
    mel_sources(online, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(online, "test");
    mel_depends(online, "audio");
    mel_depends(online, "core");
    mel_depends(online, "allocator");
    mel_depends(online, "collection");
    mel_depends(online, "thread");

    Mel_Target* event = mel_add_test(b, "audio-event");
    mel_sources(event, ALWAYS, "test/test_voice_end_future.c");
    mel_sources(event, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(event, "test");
    mel_depends(event, "audio");
    mel_depends(event, "core");
    mel_depends(event, "allocator");
    mel_depends(event, "collection");
    mel_depends(event, "future");
    mel_depends(event, "event");
    mel_depends(event, "executor");

    Mel_Target* bench = mel_add_test(b, "audio-bench");
    mel_sources(bench, ALWAYS, "test/bench_mix.c");
    mel_sources(bench, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(bench, "test");
    mel_depends(bench, "audio");
    mel_depends(bench, "core");
    mel_depends(bench, "allocator");
    mel_depends(bench, "collection");
}
