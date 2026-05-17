#include <debug/stacktrace.h>

#include <core/platform.h>
#include <string.h>
#include "string/string.str8.h"

#include <core/compiler.h>

#include <Windows.h>
#include <dbghelp.h>

static HANDLE current_process;

// TODO: this stacktrace init seems like it's going to be a pain in the ass if it's always enabled like this. maybe we should have a way to not get initialized?

MEL_CONSTRUCTOR
static void stacktrace_init(void) {
    current_process = GetCurrentProcess();
    SymInitialize(current_process, NULL, TRUE);
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
}

MEL_DESTRUCTOR
static void stacktrace_shutdown(void) {
    SymCleanup(current_process);
}

static bool translate_address(DWORD64 address, str8* str, Mel_Alloc* alloc) {
    if (str == NULL) return true;

    ULONG64 storage[(sizeof(SYMBOL_INFO) + MAX_SYM_NAME + sizeof(ULONG64) - 1) / sizeof(ULONG64)];
    PSYMBOL_INFO symbol = (PSYMBOL_INFO)storage;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;

    DWORD64 displacement = 0;
    if (!SymFromAddr(current_process, address, &displacement, symbol)) {
        return false;
    }

    usize name_len = symbol->NameLen;
    char* buf = mel_alloc(alloc, name_len + 1);
    if (buf == NULL) return false;

    memcpy(buf, symbol->Name, name_len);
    buf[name_len] = '\0';
    str->data = (u8*)buf;
    str->len = (size)name_len;

    return true;
}

bool mel__platform_stacktrace_capture(Mel_Stacktrace* stacktrace, usize skip, size keep, Mel_Alloc* alloc) {
    if (stacktrace == NULL) return false;

    void** backtrace_buffer = mel_alloc(alloc, (usize)keep * sizeof(void*));
    if (backtrace_buffer == NULL) return false;

    WORD num_frames = CaptureStackBackTrace((DWORD)skip, (DWORD)keep, backtrace_buffer, NULL);

    stacktrace->frames = mel_alloc(alloc, num_frames * sizeof(Mel_Stackframe));
    stacktrace->frame_count = 0;
    if (stacktrace->frames == NULL) {
        goto fail;
    }

    for (usize i = 0; i < num_frames; i++) {
        str8* fn_name = &stacktrace->frames[i].function_name;
        if (!translate_address((DWORD64)backtrace_buffer[i], fn_name, alloc)) {
            goto fail;
        }
        stacktrace->frame_count = i + 1;

        if (str8_equals(*fn_name, S8("main"))) {
            break;
        }
    }

    mel_dealloc(alloc, backtrace_buffer);
    return true;

fail:
    for (usize i = 0; i < stacktrace->frame_count; i++) {
        mel_dealloc(alloc, stacktrace->frames[i].function_name.data);
    }
    mel_dealloc(alloc, stacktrace->frames);
    stacktrace->frames = NULL;
    stacktrace->frame_count = 0;
    mel_dealloc(alloc, backtrace_buffer);
    return false;
}
