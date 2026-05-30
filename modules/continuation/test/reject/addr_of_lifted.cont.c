#include <continuation/cont.h>
#include <core/types.h>

mel_cont(bad_addr, (i32 n), i32)
{
    i32 acc = 0;
    i32* p  = &acc;
    mel_cont_yield(acc);
    *p += n;
    mel_cont_return(acc);
}
