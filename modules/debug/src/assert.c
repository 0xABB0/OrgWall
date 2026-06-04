#include <debug/assert.h>
#include <debug/debug.h>

#include <debug/stacktrace.h>

#include <allocator/heap.h>

#include "assert_backend.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

static _Atomic(Mel_Assert_Handler) g_handler = NULL;
static _Atomic(void*)              g_handler_user = NULL;

void mel_assert_install_handler(Mel_Assert_Handler handler, void* user)
{
    atomic_store_explicit(&g_handler_user, user, memory_order_relaxed);
    atomic_store_explicit(&g_handler, handler, memory_order_release);
}

Mel_Assert_Handler_Slot mel_assert_handler(void)
{
    Mel_Assert_Handler h = atomic_load_explicit(&g_handler, memory_order_acquire);
    void*              u = atomic_load_explicit(&g_handler_user, memory_order_relaxed);
    return (Mel_Assert_Handler_Slot){ .handler = h, .user = u };
}

void mel_abort(void)
{
    fflush(stderr);
    fflush(stdout);
    abort();
}

static const char* mel__assert_level_name(u32 level)
{
    switch (level)
    {
    case MEL_ASSERT_LEVEL_PARANOID:
        return "paranoid";
    case MEL_ASSERT_LEVEL_DEBUG:
        return "debug";
    case MEL_ASSERT_LEVEL_RELEASE:
        return "release";
    default:
        return "disabled";
    }
}

Mel_Assert_Response mel_assert_default_handler(const Mel_Assert_Report* report, void* user)
{
    (void)user;

    const Mel_Alloc* alloc = mel_alloc_heap();
    str8             trace = report->stack != NULL ? mel_stacktrace_format(report->stack, (Mel_Alloc*)alloc) : STR8_EMPTY;

    fprintf(stderr, "Assertion failed [%s]: %.*s\n  %.*s\n", mel__assert_level_name(report->level), (int)report->condition.len, report->condition.data, (int)report->location.len, report->location.data);
    if (report->message.len > 0)
        fprintf(stderr, "  %.*s\n", (int)report->message.len, report->message.data);
    if (trace.len > 0)
        fprintf(stderr, "%.*s\n", (int)trace.len, trace.data);
    fflush(stderr);

    if (trace.data != NULL)
        mel_dealloc(alloc, trace.data);

#if MEL_DEBUG
    return MEL_ASSERT_RESPONSE_BREAK | MEL_ASSERT_RESPONSE_ABORT;
#else
    return MEL_ASSERT_RESPONSE_ABORT;
#endif
}

bool mel_assert_interactive_available(void) { return mel__assert_dialog_available() || mel__assert_prompt_available(); }

Mel_Assert_Response mel_assert_interactive_handler(const Mel_Assert_Report* report, void* user)
{
    (void)user;
    if (mel__assert_dialog_available())
        return mel__assert_dialog(report);
    if (mel__assert_prompt_available())
        return mel__assert_prompt(report);
    return mel_assert_default_handler(report, user);
}

#if MEL_ASSERT_LEVEL >= MEL_ASSERT_LEVEL_RELEASE

Mel_Assert_Response mel__assert_report(u32 level, str8 condition, str8 location, str8 message)
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    Mel_Stacktrace stack = { 0 };
    bool           captured = mel_stacktrace_capture(&stack, 2, 64, (Mel_Alloc*)alloc);

    Mel_Assert_Report report = {
        .condition = condition,
        .location = location,
        .message = message,
        .level = level,
        .stack = captured ? &stack : NULL,
    };

    Mel_Assert_Handler_Slot slot = mel_assert_handler();
    Mel_Assert_Response     response;
    if (slot.handler != NULL)
        response = slot.handler(&report, slot.user);
    else
        response = mel_assert_default_handler(&report, slot.user);

    if (captured)
        mel_stacktrace_free(&stack);

    return response;
}

void mel_assert_fail(str8 condition, str8 location)
{
    Mel_Assert_Response r = mel__assert_report(MEL_ASSERT_LEVEL_DEBUG, condition, location, STR8_EMPTY);
    if (mel_assert_response_abort(r))
        mel_abort();
}

#endif
