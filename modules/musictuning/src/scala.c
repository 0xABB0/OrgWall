#include <musictuning/scala.h>

#include <assert.h>
#include <stdlib.h>
#include <collection/array.h>

typedef Mel_Array(char) Mel_Scala_Buf;

static bool mel_scala__next_line(str8* rest, str8* line)
{
    if (rest->len == 0)
        return false;
    size nl = str8_find(*rest, S8("\n"));
    if (nl < 0)
    {
        *line = *rest;
        *rest = STR8_EMPTY;
    }
    else
    {
        *line = str8_prefix(*rest, nl);
        *rest = str8_suffix(*rest, rest->len - nl - 1);
    }
    *line = str8_trim(*line);
    return true;
}

static bool mel_scala__is_comment(str8 line) { return line.len > 0 && line.data[0] == '!'; }

static str8 mel_scala__token(str8 line)
{
    size i = 0;
    while (i < line.len && line.data[i] != ' ' && line.data[i] != '\t')
        i++;
    return str8_prefix(line, i);
}

static bool mel_scala__set_str(mpfr_ptr out, str8 token, const Mel_Alloc* alloc)
{
    if (token.len == 0)
        return false;
    const char* cstr = str8_to_cstr_alloc(token, alloc);
    int         rc = mpfr_set_str(out, cstr, 10, MPFR_RNDN);
    mel_dealloc(alloc, (void*)cstr);
    return rc == 0;
}

static bool mel_scala__parse_value(mpfr_ptr out, str8 token, const Mel_Alloc* alloc)
{
    size slash = str8_find(token, S8("/"));
    if (slash >= 0)
    {
        mpfr_t    den;
        mp_limb_t den_limbs[MEL_REAL_LIMBS];
        mel_real_scratch(den, den_limbs);
        if (!mel_scala__set_str(out, str8_prefix(token, slash), alloc))
            return false;
        if (!mel_scala__set_str(den, str8_suffix(token, token.len - slash - 1), alloc))
            return false;
        if (mpfr_sgn(den) <= 0 || mpfr_sgn(out) <= 0)
            return false;
        mpfr_div(out, out, den, MPFR_RNDN);
        return true;
    }

    size dot = str8_find(token, S8("."));
    if (dot >= 0)
    {
        if (!mel_scala__set_str(out, token, alloc))
            return false;
        mpfr_div_ui(out, out, 1200, MPFR_RNDN);
        mpfr_exp2(out, out, MPFR_RNDN);
        return true;
    }

    if (!mel_scala__set_str(out, token, alloc))
        return false;
    return mpfr_sgn(out) > 0;
}

bool mel_scala_parse(Mel_Tuning* out, const Mel_Alloc* alloc, str8 data, Mel_Hz ref_frequency)
{
    str8 rest = data;
    str8 line;

    bool desc_seen = false;
    while (mel_scala__next_line(&rest, &line))
    {
        if (mel_scala__is_comment(line))
            continue;
        desc_seen = true;
        break;
    }
    if (!desc_seen)
        return false;

    i64 count = 0;
    {
        bool count_seen = false;
        while (mel_scala__next_line(&rest, &line))
        {
            if (mel_scala__is_comment(line) || line.len == 0)
                continue;
            const char* cstr = str8_to_cstr_alloc(mel_scala__token(line), alloc);
            char*       end = NULL;
            count = strtoll(cstr, &end, 10);
            count_seen = end && *end == '\0' && end != cstr;
            mel_dealloc(alloc, (void*)cstr);
            break;
        }
        if (!count_seen || count <= 0 || count > UINT32_MAX)
            return false;
    }

    Mel_Tuning t = mel_tuning_custom(alloc, (u32)count, ref_frequency);

    i64 value_idx = 0;
    while (value_idx < count && mel_scala__next_line(&rest, &line))
    {
        if (mel_scala__is_comment(line) || line.len == 0)
            continue;

        MEL_REAL_PROTECT_FLAGS;
        mpfr_t    value;
        mp_limb_t value_limbs[MEL_REAL_LIMBS];
        mel_real_scratch(value, value_limbs);

        if (!mel_scala__parse_value(value, mel_scala__token(line), alloc))
        {
            mel_tuning_free(&t);
            return false;
        }

        if (value_idx < count - 1)
            mel_tuning_custom_set_step(&t, (u32)(value_idx + 1), value);
        else
            mel_tuning_custom_set_eq_ratio(&t, value);
        value_idx++;
    }

    if (value_idx != count)
    {
        mel_tuning_free(&t);
        return false;
    }

    *out = t;
    return true;
}

static void mel_scala__push(Mel_Scala_Buf* buf, str8 s)
{
    for (size i = 0; i < s.len; i++)
        mel_array_push(buf, (char)s.data[i]);
}

str8 mel_scala_export(const Mel_Tuning* t, str8 description, const Mel_Alloc* alloc)
{
    if (t->period == 0)
        return STR8_EMPTY;

    Mel_Scala_Buf buf;
    mel_array_init(&buf, alloc);

    str8 header = str8_fmt_alloc(alloc, "%.*s\n%u\n", (int)description.len, (const char*)description.data, t->period);
    mel_scala__push(&buf, header);
    mel_dealloc(alloc, header.data);

    Mel_Hz base = mel_tuning_frequency_for_index(t, 0);

    for (u32 i = 1; i <= t->period; i++)
    {
        MEL_REAL_PROTECT_FLAGS;
        mpfr_t    ratio, cents;
        mp_limb_t ratio_limbs[MEL_REAL_LIMBS];
        mp_limb_t cents_limbs[MEL_REAL_LIMBS];
        mel_real_scratch(ratio, ratio_limbs);
        mel_real_scratch(cents, cents_limbs);

        Mel_Hz f = mel_tuning_frequency_for_index(t, (i64)i);
        mel_freq_ratio(ratio, f, base);
        mpfr_log2(cents, ratio, MPFR_RNDN);
        mpfr_mul_ui(cents, cents, 1200, MPFR_RNDN);

        int   len = mpfr_snprintf(NULL, 0, "%.6Rf", cents);
        char* line = mel_alloc(alloc, (usize)len + 2);
        mpfr_snprintf(line, (usize)len + 1, "%.6Rf", cents);
        line[len] = '\n';
        mel_scala__push(&buf, str8_from_parts((u8*)line, len + 1));
        mel_dealloc(alloc, line);
    }

    return str8_from_parts((u8*)buf.items, (size)buf.count);
}
