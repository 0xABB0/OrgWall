#include <barcode/rs.h>

bool mel_rs_generate(const mel_gf* f, u16 alpha, u32 first_root, const u16* data, usize n_data, usize n_ecc, u16* ecc_out)
{
    if (f == NULL || data == NULL || ecc_out == NULL || n_ecc == 0)
    {
        return false;
    }
    const Mel_Alloc* a = f->allocator;

    u16* gen = mel_alloc(a, sizeof(u16) * (n_ecc + 1));
    u16* rem = mel_alloc(a, sizeof(u16) * (n_data + n_ecc));
    if (gen == NULL || rem == NULL)
    {
        if (gen != NULL)
        {
            mel_dealloc(a, gen);
        }
        if (rem != NULL)
        {
            mel_dealloc(a, rem);
        }
        return false;
    }

    gen[0] = 1;
    usize len = 1;
    for (usize i = 0; i < n_ecc; ++i)
    {
        u16 root = mel_gf_pow(f, alpha, first_root + (u32)i);
        gen[len] = f->sub(f, 0, f->mul(f, root, gen[len - 1]));
        for (usize t = len - 1; t >= 1; --t)
        {
            gen[t] = f->sub(f, gen[t], f->mul(f, root, gen[t - 1]));
        }
        len += 1;
    }

    for (usize i = 0; i < n_data; ++i)
    {
        rem[i] = data[i];
    }
    for (usize i = 0; i < n_ecc; ++i)
    {
        rem[n_data + i] = 0;
    }

    for (usize i = 0; i < n_data; ++i)
    {
        u16 coef = rem[i];
        if (coef == 0)
        {
            continue;
        }
        for (usize t = 0; t <= n_ecc; ++t)
        {
            rem[i + t] = f->sub(f, rem[i + t], f->mul(f, gen[t], coef));
        }
    }

    for (usize i = 0; i < n_ecc; ++i)
    {
        ecc_out[i] = f->sub(f, 0, rem[n_data + i]);
    }

    mel_dealloc(a, gen);
    mel_dealloc(a, rem);
    return true;
}
