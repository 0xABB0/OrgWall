#include <coro/coro.h>
#include <core/types.h>

mel_coro(bad_goto, (i32 n), void)
{
    i32 i = 0;
again:
    mel_coro_yield();
    i++;
    if (i < n)
        goto again;
    mel_coro_return();
}
