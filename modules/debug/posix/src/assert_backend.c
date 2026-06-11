#include "../../src/assert_backend.h"

#include <debug/stacktrace.h>

#include <allocator/heap.h>

#include <stdio.h>
#include <unistd.h>

bool mel__assert_prompt_available(void) { return isatty(STDIN_FILENO) == 1 && isatty(STDERR_FILENO) == 1; }

Mel_Assert_Response mel__assert_prompt(const Mel_Assert_Report* report)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    str8             trace = report->stack != NULL ? mel_stacktrace_format(report->stack, (Mel_Alloc*)alloc) : STR8_EMPTY;

    fprintf(stderr, "Assertion failed: %.*s\n  %.*s\n", (int)report->condition.len, report->condition.data, (int)report->location.len, report->location.data);
    if (report->message.len > 0)
        fprintf(stderr, "  %.*s\n", (int)report->message.len, report->message.data);
    if (trace.len > 0)
        fprintf(stderr, "%.*s\n", (int)trace.len, trace.data);
    if (trace.data != NULL)
        mel_dealloc(alloc, trace.data);

    for (;;)
    {
        fprintf(stderr, "[r]etry  [i]gnore once  [I]gnore forever  [b]reak  [a]bort > ");
        fflush(stderr);

        int c = fgetc(stdin);
        if (c == EOF)
            return MEL_ASSERT_RESPONSE_ABORT;

        int rest;
        while ((rest = fgetc(stdin)) != '\n' && rest != EOF)
            ;

        switch (c)
        {
        case 'r':
            return MEL_ASSERT_RESPONSE_RETRY;
        case 'i':
            return MEL_ASSERT_RESPONSE_IGNORE_ONCE;
        case 'I':
            return MEL_ASSERT_RESPONSE_IGNORE_FOREVER;
        case 'b':
            return MEL_ASSERT_RESPONSE_BREAK;
        case 'a':
            return MEL_ASSERT_RESPONSE_BREAK | MEL_ASSERT_RESPONSE_ABORT;
        default:
            break;
        }
    }
}
