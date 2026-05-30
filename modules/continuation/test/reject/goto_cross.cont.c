#include <continuation/cont.h>
#include <core/types.h>

mel_cont(bad_goto, (i32 n), void)
{
    i32 i = 0;
again:
    mel_cont_yield();
    i++;
    if (i < n) goto again;
    mel_cont_return();
}
