#include <continuation/cont.h>
#include <core/types.h>

mel_cont(repeat_sum, (i32 n), i32)
{
    i32 total = 0;
    i32 k     = 0;
    do
    {
        total += k;
        mel_cont_yield(total);
        k++;
    } while (k < n);
    mel_cont_return(total);
}
