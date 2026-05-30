#include "relay.gen.h"
#include <core/types.h>

Mel_Cont_Suspended child_seq__resume(Mel_Cont_Frame_child_seq* __f, int* __f_out)
{
    switch (__f->state)
    {
    case MEL_CONT_STATE_START:;

    { *__f_out = (__f->base + 1); __f->state = 1; return true; case 1:; }
    { *__f_out = (__f->base + 2); __f->state = 2; return true; case 2:; }
    { __f->__ret = (0); __f->state = MEL_CONT_STATE_DONE; return false; }

    default:;
    }
    __f->state = MEL_CONT_STATE_DONE;
    return false;
}


Mel_Cont_Suspended relay__resume(Mel_Cont_Frame_relay* __f, int* __f_out)
{
    switch (__f->state)
    {
    case MEL_CONT_STATE_START:;

    { *__f_out = (__f->base); __f->state = 1; return true; case 1:; }
    __f->c = (Mel_Cont_Frame_child_seq){0};
    __f->c.base                     = __f->base;
    { for (;;) { if (!child_seq__resume(&(__f->c), __f_out)) break; __f->state = 2; return true; case 2:; } }
    { *__f_out = (__f->base + 100); __f->state = 3; return true; case 3:; }
    { __f->__ret = (__f->base); __f->state = MEL_CONT_STATE_DONE; return false; }

    default:;
    }
    __f->state = MEL_CONT_STATE_DONE;
    return false;
}


