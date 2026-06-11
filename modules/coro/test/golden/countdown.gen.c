#include "countdown.coro.h"

Mel_Coro_Suspended countdown__resume(Mel_Coro_Frame_countdown* __f)
{
    switch (__f->state)
    {
    case MEL_CORO_STATE_START:;

    while (__f->from > 0)
    {
        { __f->state = 1; return true; case 1:; }
        __f->from--;
    }

    default:;
    }
    __f->state = MEL_CORO_STATE_DONE;
    return false;
}


