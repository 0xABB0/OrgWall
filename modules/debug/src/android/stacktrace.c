#include <debug/stacktrace.h>

#include <core/platform.h>
#include <string.h>
#include "string/string.str8.h"

#include <core/compiler.h>

#if !MEL_PLATFORM_ANDROID
    #error "This file should only be compiled on Android"
#endif

#include <unwind.h>
#include <dlfcn.h>
#include <stdint.h>

typedef struct Unwind_Ctx Unwind_Ctx;
struct Unwind_Ctx {
    void** buffer;
    usize  capacity;
    usize  count;
    usize  skip;
};

static _Unwind_Reason_Code unwind_cb(struct _Unwind_Context* uctx, void* arg) {
    Unwind_Ctx* ctx = (Unwind_Ctx*)arg;
    uintptr_t ip = _Unwind_GetIP(uctx);
    if (ip == 0) return _URC_END_OF_STACK;
    if (ctx->skip > 0) {
        ctx->skip--;
        return _URC_NO_REASON;
    }
    if (ctx->count >= ctx->capacity) return _URC_END_OF_STACK;
    ctx->buffer[ctx->count++] = (void*)ip;
    return _URC_NO_REASON;
}

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
    stacktrace->alloc = alloc;

    void* backtrace_buffer[keep];

    Unwind_Ctx ctx = {
        .buffer   = backtrace_buffer,
        .capacity = (usize)keep,
        .count    = 0,
        .skip     = skip,
    };
    _Unwind_Backtrace(unwind_cb, &ctx);

    usize num_frames = ctx.count;
    if (num_frames == 0) {
        stacktrace->frames = NULL;
        stacktrace->frame_count = 0;
        return true;
    }

    stacktrace->frames = mel_alloc(alloc, num_frames * sizeof(Mel_Stackframe));
    stacktrace->frame_count = 0;
    if (stacktrace->frames == NULL) {
        goto fail;
    }

    for (usize i = 0; i < num_frames; i++) {

        Mel_Stackframe* frame = &stacktrace->frames[i];
        frame->address = backtrace_buffer[i];

#if MEL_STACKTRACE_HAS_SOURCE_INFO
        if (!translate_address_to_source_info(backtrace_buffer[i], frame, alloc)) goto fail;
#endif

#if MEL_STACKTRACE_HAS_FUNCTION_NAMES
        str8* fn_name = &frame->function_name;
        if (!translate_address_to_fn_name(backtrace_buffer[i], fn_name, alloc)) {
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
