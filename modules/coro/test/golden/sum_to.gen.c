#include "sum_to.coro.h"

Mel_Coro_Suspended sum_to__resume(Mel_Coro_Frame_sum_to* __f, long long* __f_out)
{
    switch (__f->state)
    {
    case MEL_CORO_STATE_START:;

        __f->acc = 0;
        for (__f->i = 0; __f->i < __f->n; __f->i++)
        {
            __f->acc += __f->i;
            {
                *__f_out = (__f->acc);
                __f->state = 1;
                return true;
            case 1:;
            }
        }
        {
            __f->__ret = (__f->acc);
            __f->state = MEL_CORO_STATE_DONE;
            return false;
        }

    default:;
    }
    __f->state = MEL_CORO_STATE_DONE;
    return false;
}
