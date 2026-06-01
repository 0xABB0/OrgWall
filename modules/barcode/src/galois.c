#include <barcode/galois.h>

static u16 mel__gf2_addsub(const mel_gf* f, u16 a, u16 b)
{
    (void)f;
    return a ^ b;
}

static u16 mel__gf2_mul(const mel_gf* f, u16 a, u16 b)
{
    if (a == 0 || b == 0)
    {
        return 0;
    }
    return f->exp[f->log[a] + f->log[b]];
}

static u16 mel__gfp_add(const mel_gf* f, u16 a, u16 b) { return (u16)(((u32)a + (u32)b) % f->order); }

static u16 mel__gfp_sub(const mel_gf* f, u16 a, u16 b) { return (u16)(((u32)a + (u32)f->order - (u32)b) % f->order); }

static u16 mel__gfp_mul(const mel_gf* f, u16 a, u16 b) { return (u16)(((u32)a * (u32)b) % f->order); }

bool mel_gf_binary_init(mel_gf* f, u16 order, u16 primitive_poly, const Mel_Alloc* allocator)
{
    if (allocator == NULL || order < 2)
    {
        return false;
    }
    u32  n = (u32)order - 1;
    u16* exp = mel_alloc(allocator, sizeof(u16) * 2 * n);
    u16* log = mel_calloc(allocator, sizeof(u16) * order);
    if (exp == NULL || log == NULL)
    {
        if (exp != NULL)
        {
            mel_dealloc(allocator, exp);
        }
        if (log != NULL)
        {
            mel_dealloc(allocator, log);
        }
        return false;
    }

    u16 x = 1;
    for (u32 i = 0; i < n; ++i)
    {
        exp[i] = x;
        log[x] = (u16)i;
        u32 nx = (u32)x << 1;
        if (nx & (u32)order)
        {
            nx ^= (u32)primitive_poly;
        }
        x = (u16)nx;
    }
    for (u32 i = n; i < 2 * n; ++i)
    {
        exp[i] = exp[i - n];
    }

    f->order = order;
    f->add = mel__gf2_addsub;
    f->sub = mel__gf2_addsub;
    f->mul = mel__gf2_mul;
    f->exp = exp;
    f->log = log;
    f->allocator = allocator;
    return true;
}

bool mel_gf_prime_init(mel_gf* f, u16 order, const Mel_Alloc* allocator)
{
    if (allocator == NULL || order < 2)
    {
        return false;
    }
    f->order = order;
    f->add = mel__gfp_add;
    f->sub = mel__gfp_sub;
    f->mul = mel__gfp_mul;
    f->exp = NULL;
    f->log = NULL;
    f->allocator = allocator;
    return true;
}

void mel_gf_free(mel_gf* f)
{
    if (f->exp != NULL)
    {
        mel_dealloc(f->allocator, f->exp);
    }
    if (f->log != NULL)
    {
        mel_dealloc(f->allocator, f->log);
    }
    f->exp = NULL;
    f->log = NULL;
    f->order = 0;
    f->allocator = NULL;
}

u16 mel_gf_add(const mel_gf* f, u16 a, u16 b) { return f->add(f, a, b); }

u16 mel_gf_sub(const mel_gf* f, u16 a, u16 b) { return f->sub(f, a, b); }

u16 mel_gf_mul(const mel_gf* f, u16 a, u16 b) { return f->mul(f, a, b); }

u16 mel_gf_pow(const mel_gf* f, u16 base, u32 exp)
{
    u16 result = 1;
    u16 b = base;
    while (exp != 0)
    {
        if (exp & 1u)
        {
            result = f->mul(f, result, b);
        }
        b = f->mul(f, b, b);
        exp >>= 1;
    }
    return result;
}

u16 mel_gf_inv(const mel_gf* f, u16 a)
{
    assert(a != 0);
    return mel_gf_pow(f, a, (u32)f->order - 2);
}
