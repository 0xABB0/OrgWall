#include <debug/debug.h>

#include <allocator.heap/heap.h>

Mel_Assert_Dialog_Result mel__native_assert_dialog(str8 text, str8 caption);

Mel_Assert_Dialog_Result mel_assert_dialog(bool condition, str8 message, str8 detail_message, Mel_Stacktrace* stack_frame) {
    str8_fmt(mel_alloc_heap(), "{}\n{}\n{}", message, detail_message, stack_trace(stack_frame));
}
