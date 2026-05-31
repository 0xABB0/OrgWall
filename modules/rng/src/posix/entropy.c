#include <rng/entropy.h>

#include <sys/random.h>

bool mel_rng_entropy(void* dst, usize bytes)
{
    u8*   p = (u8*)dst;
    usize n = bytes;
    while (n > 0)
    {
        usize chunk = n < 256 ? n : 256;
        if (getentropy(p, chunk) != 0)
            return false;
        p += chunk;
        n -= chunk;
    }
    return true;
}
