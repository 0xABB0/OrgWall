#include <debug/debug.h>

#include <allocator.heap/heap.h>

Mel_Assert_Dialog_Result mel__native_assert_dialog(str8 text, str8 caption);

Mel_Assert_Dialog_Result mel_assert_dialog(bool condition, str8 message, str8 detail_message, Mel_Stacktrace* stack_frame) {
    const Mel_Alloc* alloc = mel_alloc_heap();
    str8 trace = mel_stacktrace_format(stack_frame, (Mel_Alloc*)alloc);
    str8 text = str8_fmt_alloc(alloc, "%.*s\n%.*s\n%.*s",
        (int)message.len, message.data,
        (int)detail_message.len, detail_message.data,
        (int)trace.len, trace.data);
    Mel_Assert_Dialog_Result result = mel__native_assert_dialog(text, S8("Assertion failed"));
    mel_dealloc(alloc, text.data);
    mel_dealloc(alloc, trace.data);
    return result;
}
