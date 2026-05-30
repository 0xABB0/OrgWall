#include <continuation/cont.h>
#include <core/types.h>

mel_cont(child_seq, (i32 base), i32)
{
    mel_cont_yield(base + 1);
    mel_cont_yield(base + 2);
    mel_cont_return(0);
}

mel_cont(relay, (i32 base), i32)
{
    mel_cont_yield(base);
    Mel_Cont_Frame_child_seq c = {0};
    c.base                     = base;
    mel_cont_await(c);
    mel_cont_yield(base + 100);
    mel_cont_return(base);
}
