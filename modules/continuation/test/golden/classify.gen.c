#include "classify.cont.h"

Mel_Cont_Suspended classify__resume(Mel_Cont_Frame_classify* __f, int* __f_out)
{
    switch (__f->state)
    {
    case MEL_CONT_STATE_START:;

    __f->seen = 0;
    if (__f->n > 0)
    {
        __f->seen = 1;
        { *__f_out = (__f->seen); __f->state = 1; return true; case 1:; }
    }
    else
    {
        __f->seen = -1;
        { *__f_out = (__f->seen); __f->state = 2; return true; case 2:; }
    }
    { *__f_out = (__f->seen + __f->n); __f->state = 3; return true; case 3:; }
    { __f->__ret = (__f->seen); __f->state = MEL_CONT_STATE_DONE; return false; }

    default:;
    }
    __f->state = MEL_CONT_STATE_DONE;
    return false;
}


