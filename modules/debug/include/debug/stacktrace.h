#pragma once

#include "stackframe.h"

typedef struct Mel_Stacktrace Mel_Stacktrace;

struct Mel_Stacktrace {
    Mel_Stackframe* frames;
    usize frame_count;
};

bool mel_stacktrace_capture(Mel_Stacktrace* stacktrace, usize skip, size keep, Mel_Alloc* alloc);
