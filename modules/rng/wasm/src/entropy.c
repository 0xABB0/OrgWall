#include <rng/entropy.h>

#include <core/platform.h>

#if !MEL_PLATFORM_EMSCRIPTEN
#error "wasm/emscripten-only translation unit"
#endif

#include <emscripten.h>

EM_JS(int, mel_rng__crypto_random, (void* dst, int bytes), {
    try {
        var buf = new Uint8Array(HEAPU8.buffer, dst, bytes);
        crypto.getRandomValues(buf);
        return 1;
    } catch (e) {
        return 0;
    }
});

bool mel_rng_entropy(void* dst, usize bytes)
{
    u8*   p = (u8*)dst;
    usize n = bytes;
    while (n > 0)
    {
        int   chunk = n > 65536 ? 65536 : (int)n;
        if (!mel_rng__crypto_random(p, chunk))
            return false;
        p += chunk;
        n -= (usize)chunk;
    }
    return true;
}
