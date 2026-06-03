#include <debug/stacktrace.h>

#include <core/platform.h>

#if !MEL_PLATFORM_IOS
#error "this shim is iOS-only test scaffolding"
#endif

#include <allocator/allocator.h>
#include <execinfo.h>
#include <dlfcn.h>
#include <limits.h>

bool mel__platform_stacktrace_capture(Mel_Stacktrace* stacktrace, usize skip, size keep, Mel_Alloc* alloc)
{
    keep = keep > INT_MAX ? INT_MAX : keep;
    stacktrace->alloc = alloc;

    usize total = skip + (usize)keep;
    void* buffer[total];

    int captured = backtrace(buffer, (int)total);
    if (captured <= (int)skip)
    {
        stacktrace->frames = NULL;
        stacktrace->frame_count = 0;
        return true;
    }

    usize  num_frames = (usize)captured - skip;
    void** start = buffer + skip;

    stacktrace->frames = mel_alloc(alloc, num_frames * sizeof(Mel_Stackframe));
    stacktrace->frame_count = 0;
    if (stacktrace->frames == NULL)
        return false;

    for (usize i = 0; i < num_frames; i++)
    {
        Mel_Stackframe* frame = &stacktrace->frames[i];
        frame->address = start[i];
#if MEL_STACKTRACE_HAS_FUNCTION_NAMES
        Dl_info info;
        if (dladdr(start[i], &info) != 0 && info.dli_sname != NULL)
        {
            if (!str8_clone_cstr(&frame->function_name, info.dli_sname, alloc))
            {
                mel_stacktrace_free(stacktrace);
                return false;
            }
        }
        else
        {
            frame->function_name = (str8){ 0 };
        }
#endif
        stacktrace->frame_count = i + 1;
    }
    return true;
}
