#include <coro/coro.h>
#include <core/types.h>

mel_coro(bad_switch, (i32 n), void)
{
    switch (n)
    {
    case 0:
        mel_coro_yield();
        break;
    default:
        break;
    }
    mel_coro_return();
}
