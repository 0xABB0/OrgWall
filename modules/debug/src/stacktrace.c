#include <debug/stacktrace.h>

bool mel__platform_stacktrace_capture(Mel_Stacktrace* stacktrace, usize skip, size keep, Mel_Alloc* alloc);

bool mel_stacktrace_capture(Mel_Stacktrace* stacktrace, usize skip, size keep, Mel_Alloc* alloc) {
    if (stacktrace == NULL) return false;
    if (keep <= 0) return true; // TODO: understand if it's reasonable to have this check
    return mel__platform_stacktrace_capture(stacktrace, skip, keep, alloc);
}

void mel_stacktrace_free(Mel_Stacktrace* stacktrace) {
    for (usize i = 0; i < stacktrace->frame_count; i++) {
        mel_dealloc(stacktrace->alloc, stacktrace->frames[i].function_name.data);
    }
    mel_dealloc(stacktrace->alloc, stacktrace->frames);
    stacktrace->frames = NULL;
    stacktrace->frame_count = 0;
}