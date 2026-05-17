#include <debug/stacktrace.h>

#include <core/platform.h>
#include <string.h>
#include "string/str8.h"

#include <core/compiler.h>

#if !MEL_PLATFORM_OSX
    #error "This file should only be compiled on macOS"
#endif

#include <execinfo.h>
#include <dlfcn.h>
#include <limits.h>

static bool translate_address_to_fn_name(void* address, str8* str, Mel_Alloc* alloc) {
    if (str == NULL) return true;

    Dl_info info;
    if (dladdr(address, &info) == 0 || info.dli_sname == NULL) {
        return false;
    }

    return str8_clone_cstr(str, info.dli_sname, alloc);
}

static bool translate_address_to_source_info(void* address, Mel_Stackframe* frame, Mel_Alloc* alloc) {
    (void)address;
    (void)frame;
    (void)alloc;
    return true;
}

bool mel__platform_stacktrace_capture(Mel_Stacktrace* stacktrace, usize skip, size keep, Mel_Alloc* alloc) {
    keep = keep > INT_MAX ? INT_MAX : keep;

    stacktrace->alloc = alloc;

    usize total = skip + (usize)keep;
    void* backtrace_buffer[total];

    int captured = backtrace(backtrace_buffer, (int)total);
    if (captured <= (int)skip) {
        stacktrace->frames = NULL;
        stacktrace->frame_count = 0;
        return true;
    }

    usize num_frames = (usize)captured - skip;
    void** frames_start = backtrace_buffer + skip;

    stacktrace->frames = mel_alloc(alloc, num_frames * sizeof(Mel_Stackframe));
    stacktrace->frame_count = 0;
    if (stacktrace->frames == NULL) {
        goto fail;
    }

    for (usize i = 0; i < num_frames; i++) {

        Mel_Stackframe* frame = &stacktrace->frames[i];
        frame->address = frames_start[i];

#if MEL_STACKTRACE_HAS_SOURCE_INFO
        if (!translate_address_to_source_info(frames_start[i], frame, alloc)) goto fail;
#endif

#if MEL_STACKTRACE_HAS_FUNCTION_NAMES
        str8* fn_name = &frame->function_name;
        if (!translate_address_to_fn_name(frames_start[i], fn_name, alloc)) {
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
