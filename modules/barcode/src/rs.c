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

static u16 mel__rs_poly_eval(const mel_gf* f, const u16* coef, usize n, u16 x)
{
    u16 acc = 0;
    for (usize i = 0; i < n; ++i)
    {
        acc = f->add(f, f->mul(f, acc, x), coef[i]);
    }
    return acc;
}

static u16 mel__rs_inv(const mel_gf* f, u16 a)
{
    assert(a != 0);
    if (f->log != NULL && f->exp != NULL)
    {
        return f->exp[(u32)(f->order - 1u) - (u32)f->log[a]];
    }
    return mel_gf_pow(f, a, (u32)f->order - 2);
}

static void mel__rs_poly_mul_by_root(const mel_gf* f, u16* poly, usize* deg, u16 xl)
{
    for (usize t = *deg + 1; t >= 1; --t)
    {
        poly[t] = f->sub(f, poly[t], f->mul(f, xl, poly[t - 1]));
    }
    *deg += 1;
}

bool mel_rs_decode(const mel_gf* f, u16 alpha, u32 first_root, u16* codeword, usize n_total, usize n_ecc, const u16* erasures, usize n_erasures, usize* out_corrected, const Mel_Alloc* a)
{
    if (f == NULL || codeword == NULL || a == NULL || n_ecc == 0 || n_total < n_ecc || n_erasures > n_ecc)
    {
        return false;
    }
    if (n_erasures > 0 && erasures == NULL)
    {
        return false;
    }

    u16* eval_roots = mel_alloc(a, sizeof(u16) * n_ecc);
    u16* syn = mel_alloc(a, sizeof(u16) * n_ecc);
    if (eval_roots == NULL || syn == NULL)
    {
        mel_dealloc(a, eval_roots);
        mel_dealloc(a, syn);
        return false;
    }

    bool ok = true;
    bool clean = true;
    u16  root = mel_gf_pow(f, alpha, first_root);
    for (usize i = 0; i < n_ecc; ++i)
    {
        eval_roots[i] = root;
        syn[i] = mel__rs_poly_eval(f, codeword, n_total, root);
        if (syn[i] != 0)
        {
            clean = false;
        }
        root = f->mul(f, root, alpha);
    }

    if (clean && n_erasures == 0)
    {
        if (out_corrected != NULL)
        {
            *out_corrected = 0;
        }
        mel_dealloc(a, eval_roots);
        mel_dealloc(a, syn);
        return true;
    }

    for (usize k = 0; k < n_erasures; ++k)
    {
        if (erasures[k] >= n_total)
        {
            mel_dealloc(a, eval_roots);
            mel_dealloc(a, syn);
            return false;
        }
        for (usize j = k + 1; j < n_erasures; ++j)
        {
            assert(erasures[j] != erasures[k]);
        }
    }

    usize scratch_u16 = 9u * n_ecc + 7u;
    u16*  block = mel_calloc(a, sizeof(u16) * scratch_u16);
    if (block == NULL)
    {
        mel_dealloc(a, eval_roots);
        mel_dealloc(a, syn);
        return false;
    }
    u16* forney_syn = block;
    u16* gamma = forney_syn + n_ecc;
    u16* lambda = gamma + (n_ecc + 1u);
    u16* prev = lambda + (n_ecc + 1u);
    u16* tmp = prev + (n_ecc + 1u);
    u16* psi = tmp + (n_ecc + 1u);
    u16* omega = psi + (n_ecc + 2u);
    u16* roots = omega + (n_ecc + 1u);
    u16* err_pos = roots + n_ecc;

    gamma[0] = 1;
    usize gamma_deg = 0;
    for (usize k = 0; k < n_erasures; ++k)
    {
        u16 xl = mel_gf_pow(f, alpha, (u32)(n_total - 1 - erasures[k]));
        mel__rs_poly_mul_by_root(f, gamma, &gamma_deg, xl);
    }

    usize bm_n = n_ecc - gamma_deg;
    for (usize i = 0; i < bm_n; ++i)
    {
        u16 acc = 0;
        for (usize j = 0; j <= gamma_deg; ++j)
        {
            acc = f->add(f, acc, f->mul(f, gamma[j], syn[i + gamma_deg - j]));
        }
        forney_syn[i] = acc;
    }

    lambda[0] = 1;
    prev[0] = 1;
    usize l_len = 0;
    u16   prev_disc_inv = 1;
    usize since = 1;
    for (usize n = 0; n < bm_n; ++n)
    {
        u16 disc = forney_syn[n];
        for (usize i = 1; i <= l_len; ++i)
        {
            disc = f->add(f, disc, f->mul(f, lambda[i], forney_syn[n - i]));
        }

        if (disc == 0)
        {
            since += 1;
        }
        else if (2 * l_len <= n)
        {
            for (usize i = 0; i <= n_ecc; ++i)
            {
                tmp[i] = lambda[i];
            }
            u16 scale = f->mul(f, disc, prev_disc_inv);
            for (usize i = 0; i + since <= n_ecc; ++i)
            {
                lambda[i + since] = f->sub(f, lambda[i + since], f->mul(f, scale, prev[i]));
            }
            l_len = n + 1 - l_len;
            for (usize i = 0; i <= n_ecc; ++i)
            {
                prev[i] = tmp[i];
            }
            prev_disc_inv = mel__rs_inv(f, disc);
            since = 1;
        }
        else
        {
            u16 scale = f->mul(f, disc, prev_disc_inv);
            for (usize i = 0; i + since <= n_ecc; ++i)
            {
                lambda[i + since] = f->sub(f, lambda[i + since], f->mul(f, scale, prev[i]));
            }
            since += 1;
        }
    }

    usize lambda_deg = 0;
    for (usize i = 0; i <= n_ecc; ++i)
    {
        if (lambda[i] != 0)
        {
            lambda_deg = i;
        }
    }
    if (l_len != lambda_deg)
    {
        ok = false;
        goto done;
    }

    usize errata = lambda_deg + gamma_deg;
    if (errata > n_ecc)
    {
        ok = false;
        goto done;
    }
    for (usize i = 0; i <= errata; ++i)
    {
        u16 acc = 0;
        for (usize j = 0; j <= i; ++j)
        {
            if (j <= lambda_deg && (i - j) <= gamma_deg)
            {
                acc = f->add(f, acc, f->mul(f, lambda[j], gamma[i - j]));
            }
        }
        psi[i] = acc;
    }

    usize root_count = 0;
    u16   xl = 1;
    for (usize e = 0; e < n_total; ++e)
    {
        u16 val = psi[0];
        for (usize i = 1; i <= errata; ++i)
        {
            val = f->add(f, f->mul(f, val, xl), psi[i]);
        }
        if (val == 0)
        {
            if (root_count >= n_ecc)
            {
                ok = false;
                goto done;
            }
            roots[root_count] = xl;
            err_pos[root_count] = (u16)(n_total - 1 - e);
            root_count += 1;
        }
        xl = f->mul(f, xl, alpha);
    }

    if (root_count != errata)
    {
        ok = false;
        goto done;
    }

    usize omega_len = errata;
    for (usize i = 0; i < omega_len; ++i)
    {
        u16 acc = 0;
        for (usize j = 0; j <= i && j <= errata; ++j)
        {
            acc = f->add(f, acc, f->mul(f, syn[i - j], psi[j]));
        }
        omega[i] = acc;
    }

    usize applied = 0;
    for (usize l = 0; l < root_count; ++l)
    {
        u16 root_l = roots[l];
        u16 x_inv = mel__rs_inv(f, root_l);

        u16 omega_val = 0;
        u16 pw = 1;
        for (usize i = 0; i < omega_len; ++i)
        {
            omega_val = f->add(f, omega_val, f->mul(f, omega[i], pw));
            pw = f->mul(f, pw, x_inv);
        }

        u16 deriv = 0;
        pw = 1;
        for (usize i = 1; i <= errata; i += 2)
        {
            deriv = f->add(f, deriv, f->mul(f, psi[i], pw));
            pw = f->mul(f, pw, f->mul(f, x_inv, x_inv));
        }
        if (deriv == 0)
        {
            ok = false;
            goto done;
        }

        u16 z = f->mul(f, root_l, f->mul(f, omega_val, mel__rs_inv(f, deriv)));
        u16 xb = mel_gf_pow(f, root_l, first_root);
        u16 mag = f->mul(f, z, mel__rs_inv(f, xb));

        if (mag != 0)
        {
            codeword[err_pos[l]] = f->sub(f, codeword[err_pos[l]], mag);
            applied += 1;
        }
    }

    for (usize i = 0; i < n_ecc; ++i)
    {
        if (mel__rs_poly_eval(f, codeword, n_total, eval_roots[i]) != 0)
        {
            ok = false;
            goto done;
        }
    }

    if (out_corrected != NULL)
    {
        *out_corrected = applied;
    }

done:
    mel_dealloc(a, block);
    mel_dealloc(a, eval_roots);
    mel_dealloc(a, syn);
    return ok;
}
