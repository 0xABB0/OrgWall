#include <continuation/cont.h>
#include <core/types.h>

mel_cont(sum_to, (i64 n), i64)
{
    i64 acc = 0;
    for (i32 i = 0; i < n; i++)
    {
        acc += i;
        mel_cont_yield(acc);
    }
    mel_cont_return(acc);
}
