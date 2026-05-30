#include <continuation/cont.h>
#include <core/types.h>

mel_cont(countdown, (i32 from), void)
{
    while (from > 0)
    {
        mel_cont_yield();
        from--;
    }
}
