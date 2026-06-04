#include <debug/debug.h>

#include <debug/assert.h>

Mel_Assert_Dialog_Result mel_assert_dialog(bool condition, str8 message, str8 detail_message, Mel_Stacktrace* stack_frame)
{
    if (condition)
        return ASSERT_DIALOG_RESULT_IGNORE;

    Mel_Assert_Report report = {
        .condition = message,
        .location = detail_message,
        .message = STR8_EMPTY,
        .level = MEL_ASSERT_LEVEL_DEBUG,
        .stack = stack_frame,
    };

    Mel_Assert_Handler_Slot slot = mel_assert_handler();
    Mel_Assert_Response     response = slot.handler != NULL ? slot.handler(&report, slot.user) : mel_assert_default_handler(&report, slot.user);

    if (mel_assert_response_retry(response))
        return ASSERT_DIALOG_RESULT_RETRY;
    if (mel_assert_response_ignored(response))
        return ASSERT_DIALOG_RESULT_IGNORE;
    return ASSERT_DIALOG_RESULT_ABORT;
}
