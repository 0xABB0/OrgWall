#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "audio");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");

    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "math");
    mel_depends(lib, "thread");
    mel_depends(lib, "time");
    mel_depends(lib, "executor");
    mel_depends(lib, "future");
    mel_depends(lib, "event");
    mel_depends(lib, "channel");
    mel_depends(lib, "log");
    mel_depends(lib, "debug");
    mel_depends(lib, "string");
    mel_depends(lib, "pcm");
    mel_depends(lib, "audioout");
    mel_depends(lib, "audioplayback");
    mel_depends(lib, "audiopolicy");

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

    Mel_Target* device = mel_add_test(b, "audio-device");
    mel_includes(device, MEL_PUBLIC, ALWAYS, "include");
    mel_includes(device, MEL_PUBLIC, ALWAYS, "../audioout/include");
    mel_includes(device, MEL_PUBLIC, ALWAYS, "../audioplayback/include");
    mel_includes(device, MEL_PUBLIC, ALWAYS, "../audiopolicy/include");
    mel_sources(device, ALWAYS, "src/*.c");
    mel_sources(device, ALWAYS, "../audioout/src/audioout.c");
    mel_sources(device, ALWAYS, "../audioout/src/descriptors.c");
    mel_sources(device, ALWAYS, "../audioout/src/publish.c");
    mel_sources(device, ALWAYS, "../audioplayback/src/audioplayback.c");
    mel_sources(device, ALWAYS, "../audiopolicy/src/audiopolicy.c");
    mel_sources(device, ALWAYS, "../audiopolicy/src/descriptors.c");
    mel_sources(device, ALWAYS, "test/test_device.c");
    mel_sources(device, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(device, "test");
    mel_depends(device, "core");
    mel_depends(device, "allocator");
    mel_depends(device, "collection");
    mel_depends(device, "math");
    mel_depends(device, "thread");
    mel_depends(device, "time");
    mel_depends(device, "executor");
    mel_depends(device, "future");
    mel_depends(device, "event");
    mel_depends(device, "channel");
    mel_depends(device, "log");
    mel_depends(device, "debug");
    mel_depends(device, "string");
    mel_depends(device, "pcm");

    Mel_Target* taps = mel_add_test(b, "audio-taps");
    mel_sources(taps, ALWAYS, "test/test_taps.c");
    mel_sources(taps, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(taps, "test");
    mel_depends(taps, "audio");
    mel_depends(taps, "core");
    mel_depends(taps, "allocator");
    mel_depends(taps, "collection");

    Mel_Target* bench = mel_add_test(b, "audio-bench");
    mel_sources(bench, ALWAYS, "test/bench_mix.c");
    mel_sources(bench, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(bench, "test");
    mel_depends(bench, "audio");
    mel_depends(bench, "core");
    mel_depends(bench, "allocator");
    mel_depends(bench, "collection");
}
