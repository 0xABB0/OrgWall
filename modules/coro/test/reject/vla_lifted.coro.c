#include <coro/coro.h>
#include <core/types.h>

mel_coro(bad_vla, (i32 n), void)
{
    i32 buf[n];
    buf[0] = 1;
    mel_coro_yield();
    buf[0] = 2;
    mel_coro_return();
}
