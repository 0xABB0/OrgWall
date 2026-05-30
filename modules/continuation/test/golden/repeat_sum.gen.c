#include "repeat_sum.gen.h"
#include <core/types.h>

Mel_Cont_Suspended repeat_sum__resume(Mel_Cont_Frame_repeat_sum* __f, int* __f_out)
{
    switch (__f->state)
    {
    case MEL_CONT_STATE_START:;

    __f->total = 0;
    __f->k = 0;
    do
    {
        __f->total += __f->k;
        { *__f_out = (__f->total); __f->state = 1; return true; case 1:; }
        __f->k++;
    } while (__f->k < __f->n);
    { __f->__ret = (__f->total); __f->state = MEL_CONT_STATE_DONE; return false; }

    default:;
    }
    __f->state = MEL_CONT_STATE_DONE;
    return false;
}


