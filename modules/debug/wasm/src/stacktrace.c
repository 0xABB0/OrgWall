#include <debug/stacktrace.h>

#include <core/platform.h>

#if !MEL_PLATFORM_EMSCRIPTEN
#error "This file should only be compiled on wasm"
#endif

#include <stdio.h>

bool mel__platform_stacktrace_capture(Mel_Stacktrace* stacktrace, usize skip, size keep, Mel_Alloc* alloc)
{
    (void)skip;
    (void)keep;
    stacktrace->alloc = alloc;
    stacktrace->frames = NULL;
    stacktrace->frame_count = 0;
    fputs("stacktrace: capture unavailable on wasm (no unwinder in the emscripten sysroot)\n", stderr);
    return false;
}
