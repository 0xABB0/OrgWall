#include <rng/entropy.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <bcrypt.h>

bool mel_rng_entropy(void* dst, usize bytes)
{
    while (bytes > 0)
    {
        ULONG    chunk = bytes > 0xFFFFFFFFull ? 0xFFFFFFFFul : (ULONG)bytes;
        NTSTATUS st = BCryptGenRandom(NULL, (PUCHAR)dst, chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (st < 0)
            return false;
        dst = (u8*)dst + chunk;
        bytes -= chunk;
    }
    return true;
}
