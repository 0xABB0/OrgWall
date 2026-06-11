#include <hash/fnv.h>

#define FNV32_OFFSET_BASIS 0x811C9DC5U
#define FNV32_PRIME        0x01000193U
#define FNV64_OFFSET_BASIS 0xCBF29CE484222325ULL
#define FNV64_PRIME        0x00000100000001B3ULL

u32 mel_fnv1a32(const void* data, usize len)
{
    const u8* p = (const u8*)data;
    u32       h = FNV32_OFFSET_BASIS;
    for (usize i = 0; i < len; i++)
    {
        h ^= p[i];
        h *= FNV32_PRIME;
    }
    return h;
}

u64 mel_fnv1a64(const void* data, usize len)
{
    const u8* p = (const u8*)data;
    u64       h = FNV64_OFFSET_BASIS;
    for (usize i = 0; i < len; i++)
    {
        h ^= p[i];
        h *= FNV64_PRIME;
    }
    return h;
}
