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

static bool translate_address_to_fn_name(DWORD64 address, str8* str, Mel_Alloc* alloc) {
    if (str == NULL) return true;

    ULONG64 storage[(sizeof(SYMBOL_INFO) + MAX_SYM_NAME + sizeof(ULONG64) - 1) / sizeof(ULONG64)];
    PSYMBOL_INFO symbol = (PSYMBOL_INFO)storage;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;

    DWORD64 displacement = 0;
    if (!SymFromAddr(current_process, address, &displacement, symbol)) {
        return false;
    }

    return str8_clone_cstr(str, symbol->Name, symbol->NameLen, alloc);
}

static bool translate_address_to_source_info(DWORD64 address, Mel_Stackframe* frame, Mel_Alloc* alloc) {
    IMAGEHLP_LINE64 source_info;
    DWORD displacement;
    memset(&source_info, 0, sizeof(source_info));
    source_info.SizeOfStruct = sizeof(source_info);

    BOOL result = SymGetLineFromAddr64(current_process, address, &displacement, &source_info);

    if (result) {
        result = str8_clone_cstr(&frame->filename, source_info.FileName, alloc);
    }

    return result;
}

bool mel__platform_stacktrace_capture(Mel_Stacktrace* stacktrace, usize skip, size keep, Mel_Alloc* alloc) {
    keep = keep > USHRT_MAX ? USHRT_MAX : keep;

    stacktrace->alloc = alloc;

    void* backtrace_buffer[keep];

    WORD num_frames = CaptureStackBackTrace((DWORD)skip, (DWORD)keep, backtrace_buffer, NULL);

    stacktrace->frames = mel_alloc(alloc, num_frames * sizeof(Mel_Stackframe));
    stacktrace->frame_count = 0;
    if (stacktrace->frames == NULL) {
        goto fail;
    }

    for (usize i = 0; i < num_frames; i++) {

        Mel_Stackframe* frame = &stacktrace->frames[i];
        frame->address = backtrace_buffer[i];

#if MEL_STACKTRACE_HAS_SOURCE_INFO
    if (!translate_address_to_source_info((DWORD64)backtrace_buffer[i], frame, alloc)) goto fail;
#endif

#if MEL_STACKTRACE_HAS_FUNCTION_NAMES
        str8* fn_name = &frame->function_name;
        if (!translate_address_to_fn_name((DWORD64)backtrace_buffer[i], fn_name, alloc)) {
            goto fail;
        }
        stacktrace->frame_count = i + 1;

        if (str8_equals(*fn_name, S8("main"))) {
            break;
        }
#endif

    }

    return true;

fail:
    mel_stacktrace_free(stacktrace);
    return false;
}
