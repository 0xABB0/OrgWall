#include <debug/stacktrace.h>

#include <allocator/allocator.h>
#include <string.h>

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

str8 mel_stacktrace_format(Mel_Stacktrace* stacktrace, Mel_Alloc* alloc) {
    if (stacktrace == NULL || stacktrace->frame_count == 0) return STR8_EMPTY;

    str8* lines = mel_alloc(alloc, sizeof(str8) * stacktrace->frame_count);
    if (lines == NULL) return STR8_EMPTY;

    size total = 0;
    for (usize i = 0; i < stacktrace->frame_count; i++) {
        Mel_Stackframe* f = &stacktrace->frames[i];
#if MEL_STACKTRACE_HAS_FUNCTION_NAMES && MEL_STACKTRACE_HAS_SOURCE_INFO
        lines[i] = str8_fmt_alloc(alloc, "  at %.*s (%.*s:%zu:%zu) [%p]\n",
            (int)f->function_name.len, f->function_name.data,
            (int)f->filename.len, f->filename.data,
            f->file_line, f->column, f->address);
#elif MEL_STACKTRACE_HAS_FUNCTION_NAMES
        lines[i] = str8_fmt_alloc(alloc, "  at %.*s [%p]\n",
            (int)f->function_name.len, f->function_name.data, f->address);
#elif MEL_STACKTRACE_HAS_SOURCE_INFO
        lines[i] = str8_fmt_alloc(alloc, "  at %.*s:%zu:%zu [%p]\n",
            (int)f->filename.len, f->filename.data,
            f->file_line, f->column, f->address);
#else
        lines[i] = str8_fmt_alloc(alloc, "  at %p\n", f->address);
#endif
        total += lines[i].len;
    }

    u8* buf = mel_alloc(alloc, (usize)(total + 1));
    if (buf == NULL) {
        for (usize i = 0; i < stacktrace->frame_count; i++) mel_dealloc(alloc, lines[i].data);
        mel_dealloc(alloc, lines);
        return STR8_EMPTY;
    }

    size offset = 0;
    for (usize i = 0; i < stacktrace->frame_count; i++) {
        memcpy(buf + offset, lines[i].data, (usize)lines[i].len);
        offset += lines[i].len;
        mel_dealloc(alloc, lines[i].data);
    }
    buf[total] = '\0';
    mel_dealloc(alloc, lines);

    return (str8){ .data = buf, .len = total };
}