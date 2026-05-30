#include <continuation/cont.h>
#include <core/types.h>

mel_cont(bad_vla, (i32 n), void)
{
    i32 buf[n];
    buf[0] = 1;
    mel_cont_yield();
    buf[0] = 2;
    mel_cont_return();
}
