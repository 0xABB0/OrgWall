#include <continuation/cont.h>
#include <core/types.h>

mel_cont(bad_switch, (i32 n), void)
{
    switch (n)
    {
    case 0:
        mel_cont_yield();
        break;
    default:
        break;
    }
    mel_cont_return();
}
