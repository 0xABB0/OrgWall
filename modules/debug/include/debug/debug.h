#pragma once

#include "debug.cfg.h"

#include <debug/assert.h>
#include <debug/stacktrace.h>
#include <string/str8.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
    ASSERT_DIALOG_RESULT_ABORT,
    ASSERT_DIALOG_RESULT_RETRY,
    ASSERT_DIALOG_RESULT_IGNORE
} Mel_Assert_Dialog_Result;

Mel_Assert_Dialog_Result mel_assert_dialog(bool condition, str8 message, str8 detail_message, Mel_Stacktrace* stack_frame);

#ifdef __cplusplus
}
#endif
