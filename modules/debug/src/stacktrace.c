#include <debug/stacktrace.h>

bool mel__platform_stacktrace_capture(Mel_Stacktrace* stacktrace, usize skip, size keep, Mel_Alloc* alloc);

bool mel_stacktrace_capture(Mel_Stacktrace* stacktrace, usize skip, size keep, Mel_Alloc* alloc) {
    return mel__platform_stacktrace_capture(stacktrace, skip, keep, alloc);
}
