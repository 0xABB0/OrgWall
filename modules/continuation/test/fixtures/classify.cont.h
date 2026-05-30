#pragma once

#include <continuation/cont.h>
#include <core/types.h>

mel_cont(classify, (i32 n), i32)
{
    i32 seen = 0;
    if (n > 0)
    {
        seen = 1;
        mel_cont_yield(seen);
    }
    else
    {
        seen = -1;
        mel_cont_yield(seen);
    }
    mel_cont_yield(seen + n);
    mel_cont_return(seen);
}
