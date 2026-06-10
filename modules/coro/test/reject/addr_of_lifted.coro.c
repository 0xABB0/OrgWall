#include <coro/coro.h>
#include <core/types.h>

mel_coro(bad_addr, (i32 n), i32)
{
    i32  acc = 0;
    i32* p = &acc;
    mel_coro_yield(acc);
    *p += n;
    mel_coro_return(acc);
}
