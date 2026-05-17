#pragma once

#include "stacktrace.cfg.h"

#include <string/str8.h>

typedef struct Mel_Stackframe Mel_Stackframe;

struct Mel_Stackframe
{
    void* address;
#if MEL_STACKTRACE_HAS_FUNCTION_NAMES
    str8  function_name;
#endif

#if MEL_STACKTRACE_HAS_SOURCE_INFO
    str8  filename;
    usize file_line;
    usize column;
    usize offset;
#endif
};


typedef struct Mel_Stacktrace Mel_Stacktrace;

struct Mel_Stacktrace {
    Mel_Stackframe* frames;
    usize frame_count;

    Mel_Alloc* alloc;
};

bool mel_stacktrace_capture(Mel_Stacktrace* stacktrace, usize skip, size keep, Mel_Alloc* alloc);

void mel_stacktrace_free(Mel_Stacktrace* stacktrace);
