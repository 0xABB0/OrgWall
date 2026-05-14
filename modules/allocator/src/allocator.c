#include <allocator/allocator.h>

static Mel_Mem_Fail_Cb s_fail_cb = NULL;

void mel_mem_set_fail_callback(Mel_Mem_Fail_Cb cb)
{
    s_fail_cb = cb;
}

Mel_Mem_Fail_Cb mel__get_fail_cb(void)
{
    return s_fail_cb;
}
