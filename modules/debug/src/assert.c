#include <debug/assert.h>

#if MEL_ASSERT_ENABLED

#include <debug/stacktrace.h>

#include <allocator/heap.h>

#include <stdio.h>

void mel_assert_fail(str8 condition, str8 location)
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    Mel_Stacktrace stack = { 0 };
    mel_stacktrace_capture(&stack, 3, 64, (Mel_Alloc*)alloc);
    str8 trace = mel_stacktrace_format(&stack, (Mel_Alloc*)alloc);

    fprintf(stderr, "Assertion failed: %.*s\n  %.*s\n%.*s\n", (int)condition.len, condition.data, (int)location.len, location.data, (int)trace.len, trace.data);
    fflush(stderr);

    mel_stacktrace_free(&stack);
    mel_dealloc(alloc, trace.data);
}

#endif
