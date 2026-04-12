#pragma once

#include "core.types.h"

#define MEL_BACKTRACE_MAX_FRAMES 64

typedef struct {
    void* frames[MEL_BACKTRACE_MAX_FRAMES];
    i32 frame_count;
} Mel_Backtrace;

void mel_backtrace_init(void);
void mel_backtrace_capture(Mel_Backtrace* bt, i32 skip);
void mel_backtrace_print(Mel_Backtrace* bt);
void mel_backtrace_print_current(void);
