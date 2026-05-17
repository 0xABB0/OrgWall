#pragma once

#include "debug.cfg.h"

#include <debug/stacktrace.h>
#include <string/str8.h>

typedef enum {
    ASSERT_DIALOG_RESULT_ABORT,
    ASSERT_DIALOG_RESULT_RETRY,
    ASSERT_DIALOG_RESULT_IGNORE
} Mel_Assert_Dialog_Result;

Mel_Assert_Dialog_Result mel_assert_dialog(bool condition, str8 message, str8 detail_message, Mel_Stacktrace* stack_frame);
