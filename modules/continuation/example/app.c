#include "ticker.cont.h"

#include <stdio.h>

int main(void)
{
    Mel_Cont_Frame_ticker anim = {0};
    anim.frames                = 5;
    anim.amplitude             = 100;

    i32 value;
    while (ticker__resume(&anim, &value)) printf("frame value = %d\n", value);
    printf("final = %d\n", anim.__ret);
    return 0;
}
