#include <barcode/itf.h>

#include "paint.h"

#include <collection/array.h>
#include <string.h>

static const char* const MEL__ITF_PAT[10] = {
    "00110", "10001", "01001", "11000", "00101", "10100", "01100", "00011", "10010", "01010",
};

bool mel_itf_encode(mel_barcode_matrix* out, const char* digits, i32 height, mel_itf_opt opt, const Mel_Alloc* allocator)
{
    if (digits == NULL || height <= 0)
    {
        return false;
    }
    usize len = strlen(digits);
    if (len == 0)
    {
        return false;
    }
    for (usize i = 0; i < len; ++i)
    {
        if (digits[i] < '0' || digits[i] > '9')
        {
            return false;
        }
    }

    Mel_Array(char) buf;
    mel_array_init(&buf, allocator);
    for (usize i = 0; i < len; ++i)
    {
        mel_array_push(&buf, digits[i]);
    }

    if (opt.checksum)
    {
        i32 n = (i32)buf.count;
        i32 sum = 0;
        for (i32 i = 0; i < n; ++i)
        {
            i32 d = buf.items[i] - '0';
            i32 w = (((n - 1 - i) & 1) == 0) ? 3 : 1;
            sum += d * w;
        }
        i32 c = (10 - (sum % 10)) % 10;
        mel_array_push(&buf, (char)('0' + c));
    }

    if ((buf.count % 2) != 0)
    {
        if (!opt.pad_odd)
        {
            mel_array_free(&buf);
            return false;
        }
        mel_array_insert(&buf, 0, '0');
    }

    i32 pairs = (i32)buf.count / 2;
    i32 width = 4 + 18 * pairs + 5;
    if (!mel_barcode_matrix_init(out, width, height, allocator))
    {
        mel_array_free(&buf);
        return false;
    }
    out->quiet_zone = 10;

    mel__painter p = { out, 0 };
    mel__paint_run(&p, 1, true);
    mel__paint_run(&p, 1, false);
    mel__paint_run(&p, 1, true);
    mel__paint_run(&p, 1, false);

    for (i32 q = 0; q < pairs; ++q)
    {
        const char* bars = MEL__ITF_PAT[buf.items[2 * q] - '0'];
        const char* spaces = MEL__ITF_PAT[buf.items[2 * q + 1] - '0'];
        for (i32 k = 0; k < 5; ++k)
        {
            mel__paint_run(&p, (bars[k] == '1') ? 3 : 1, true);
            mel__paint_run(&p, (spaces[k] == '1') ? 3 : 1, false);
        }
    }

    mel__paint_run(&p, 3, true);
    mel__paint_run(&p, 1, false);
    mel__paint_run(&p, 1, true);

    mel_array_free(&buf);
    return true;
}
