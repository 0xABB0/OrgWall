#include "../assert_backend.h"

#include <debug/stacktrace.h>

#include <allocator/heap.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>

bool mel__assert_dialog_available(void) { return GetConsoleWindow() == NULL; }

static wchar_t* mel__widen(const Mel_Alloc* a, const char* s, int len)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, len, NULL, 0);
    if (n <= 0)
        return NULL;
    wchar_t* w = (wchar_t*)mel_alloc(a, (usize)(n + 1) * sizeof(wchar_t));
    if (!w)
        return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, len, w, n);
    w[n] = 0;
    return w;
}

Mel_Assert_Response mel__assert_dialog(const Mel_Assert_Report* report)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    str8             trace = report->stack != NULL ? mel_stacktrace_format(report->stack, (Mel_Alloc*)alloc) : STR8_EMPTY;

    char buf[4096];
    int  off = 0;
    off += _snprintf(buf + off, (size_t)(sizeof(buf) - off), "Assertion failed: %.*s\n%.*s", (int)report->condition.len, report->condition.data, (int)report->location.len, report->location.data);
    if (report->message.len > 0 && off < (int)sizeof(buf))
        off += _snprintf(buf + off, (size_t)(sizeof(buf) - off), "\n%.*s", (int)report->message.len, report->message.data);
    if (trace.len > 0 && off < (int)sizeof(buf))
        _snprintf(buf + off, (size_t)(sizeof(buf) - off), "\n\n%.*s", (int)trace.len, trace.data);
    buf[sizeof(buf) - 1] = 0;

    wchar_t* wtext = mel__widen(alloc, buf, -1);

    int r = MessageBoxW(NULL, wtext ? wtext : L"Assertion failed", L"Assertion failed", MB_ABORTRETRYIGNORE | MB_ICONERROR | MB_TASKMODAL | MB_SETFOREGROUND);

    if (wtext)
        mel_dealloc(alloc, wtext);
    if (trace.data != NULL)
        mel_dealloc(alloc, trace.data);

    switch (r)
    {
    case IDRETRY:
        return MEL_ASSERT_RESPONSE_RETRY;
    case IDIGNORE:
        return MEL_ASSERT_RESPONSE_IGNORE_ONCE;
    default:
        return MEL_ASSERT_RESPONSE_BREAK | MEL_ASSERT_RESPONSE_ABORT;
    }
}
