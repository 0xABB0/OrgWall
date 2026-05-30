#include "countdown.cont.h"

Mel_Cont_Suspended countdown__resume(Mel_Cont_Frame_countdown* __f)
{
    switch (__f->state)
    {
    case MEL_CONT_STATE_START:;

    while (__f->from > 0)
    {
        { __f->state = 1; return true; case 1:; }
        __f->from--;
    }

    default:;
    }
    __f->state = MEL_CONT_STATE_DONE;
    return false;
}


