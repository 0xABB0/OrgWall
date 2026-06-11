#include <compress/rle.h>

#include <allocator/allocator.h>

#include <string.h>

#define RLE_MAGIC_LEN  4
#define RLE_MAX_RUN    128
#define RLE_EOS        0x80
#define RLE_OUTBOX_CAP 512
#define RLE_FLUSH_ROOM 132

static const u8 g_rle_magic[RLE_MAGIC_LEN] = { 'M', 'R', 'L', '1' };

typedef struct
{
    const Mel_Alloc* alloc;
    bool             decompress;

    u8*   outbox;
    usize box_head;
    usize box_len;

    u8*   lit;
    usize lit_len;
    u8    run_byte;
    usize run_len;
    bool  finalized;

    usize magic_left;
    usize lit_left;
    usize run_left;
    u8    run_fill;
    bool  want_run_byte;
    bool  eos;
} Rle_Stream;

static void box_put(Rle_Stream* z, u8 b)
{
    z->outbox[z->box_head + z->box_len] = b;
    z->box_len++;
}

static usize box_drain(Rle_Stream* z, u8* out, usize out_cap)
{
    usize n = z->box_len < out_cap ? z->box_len : out_cap;
    memcpy(out, z->outbox + z->box_head, n);
    z->box_head += n;
    z->box_len -= n;
    if (z->box_len == 0)
        z->box_head = 0;
    return n;
}

static void flush_lit(Rle_Stream* z)
{
    if (z->lit_len == 0)
        return;
    box_put(z, (u8)(z->lit_len - 1));
    memcpy(z->outbox + z->box_head + z->box_len, z->lit, z->lit_len);
    z->box_len += z->lit_len;
    z->lit_len = 0;
}

static void lit_append(Rle_Stream* z, u8 b)
{
    if (z->lit_len == RLE_MAX_RUN)
        flush_lit(z);
    z->lit[z->lit_len++] = b;
}

static void settle_run(Rle_Stream* z)
{
    if (z->run_len == 0)
        return;
    if (z->run_len >= 3)
    {
        flush_lit(z);
        box_put(z, (u8)(257 - z->run_len));
        box_put(z, z->run_byte);
    }
    else
    {
        for (usize i = 0; i < z->run_len; i++)
            lit_append(z, z->run_byte);
    }
    z->run_len = 0;
}

static Mel_Compress_Step rle_step_compress(Rle_Stream* z, str8 in, bool in_last, u8* out, usize out_cap)
{
    Mel_Compress_Step step = { 0 };

    step.out_produced += box_drain(z, out, out_cap);
    if (z->box_len > 0)
    {
        step.status = MEL_COMPRESS_OK | MEL_COMPRESS_OUTPUT_FULL;
        return step;
    }

    while (step.in_consumed < (usize)in.len && z->box_len + RLE_FLUSH_ROOM <= RLE_OUTBOX_CAP)
    {
        u8 x = in.data[step.in_consumed++];
        if (z->run_len > 0 && x == z->run_byte && z->run_len < RLE_MAX_RUN)
        {
            z->run_len++;
            continue;
        }
        settle_run(z);
        z->run_byte = x;
        z->run_len = 1;
    }

    if (in_last && step.in_consumed == (usize)in.len && !z->finalized && z->box_len + RLE_FLUSH_ROOM <= RLE_OUTBOX_CAP)
    {
        settle_run(z);
        flush_lit(z);
        box_put(z, RLE_EOS);
        z->finalized = true;
    }

    step.out_produced += box_drain(z, out + step.out_produced, out_cap - step.out_produced);
    if (z->box_len > 0)
        step.status |= MEL_COMPRESS_OUTPUT_FULL;
    step.finished = z->finalized && z->box_len == 0;
    return step;
}

static Mel_Compress_Step rle_step_decompress(Rle_Stream* z, str8 in, bool in_last, u8* out, usize out_cap)
{
    Mel_Compress_Step step = { 0 };

    for (;;)
    {
        if (z->eos)
        {
            step.finished = true;
            return step;
        }
        if (z->run_left > 0 && !z->want_run_byte)
        {
            usize n = z->run_left < out_cap - step.out_produced ? z->run_left : out_cap - step.out_produced;
            if (n == 0)
            {
                step.status |= MEL_COMPRESS_OUTPUT_FULL;
                return step;
            }
            memset(out + step.out_produced, z->run_fill, n);
            step.out_produced += n;
            z->run_left -= n;
            continue;
        }
        if (z->lit_left > 0)
        {
            usize avail = (usize)in.len - step.in_consumed;
            usize room = out_cap - step.out_produced;
            usize n = z->lit_left;
            if (n > avail)
                n = avail;
            if (n > room)
                n = room;
            if (n == 0)
            {
                if (room == 0)
                {
                    step.status |= MEL_COMPRESS_OUTPUT_FULL;
                    return step;
                }
                break;
            }
            memcpy(out + step.out_produced, in.data + step.in_consumed, n);
            step.in_consumed += n;
            step.out_produced += n;
            z->lit_left -= n;
            continue;
        }
        if (step.in_consumed >= (usize)in.len)
            break;
        u8 c = in.data[step.in_consumed++];
        if (z->magic_left > 0)
        {
            if (c != g_rle_magic[RLE_MAGIC_LEN - z->magic_left])
            {
                step.status = MEL_COMPRESS_ERROR | MEL_COMPRESS_UNKNOWN_FORMAT;
                return step;
            }
            z->magic_left--;
            continue;
        }
        if (z->want_run_byte)
        {
            z->run_fill = c;
            z->want_run_byte = false;
            continue;
        }
        if (c == RLE_EOS)
        {
            z->eos = true;
            step.finished = true;
            return step;
        }
        if (c < RLE_EOS)
            z->lit_left = (usize)c + 1;
        else
        {
            z->run_left = 257 - (usize)c;
            z->want_run_byte = true;
        }
    }

    if (in_last && step.in_consumed == (usize)in.len)
        step.status = MEL_COMPRESS_ERROR | MEL_COMPRESS_TRUNCATED;
    return step;
}

static Mel_Compress_Step rle_step(Mel_Compress_Stream* s, str8 in, bool in_last, u8* out, usize out_cap)
{
    Rle_Stream* z = (Rle_Stream*)s;
    if (z->decompress)
        return rle_step_decompress(z, in, in_last, out, out_cap);
    return rle_step_compress(z, in, in_last, out, out_cap);
}

static Mel_Compress_Stream* rle_begin(Mel_Compress_Begin begin, Mel_Compress_Status* status)
{
    if (!begin.alloc)
    {
        *status = MEL_COMPRESS_ERROR | MEL_COMPRESS_BAD_STATE;
        return NULL;
    }
    if (!begin.decompress && begin.level != 1)
    {
        *status = MEL_COMPRESS_ERROR | MEL_COMPRESS_BAD_LEVEL;
        return NULL;
    }
    Rle_Stream* z = mel_alloc_type(begin.alloc, Rle_Stream);
    if (!z)
    {
        *status = MEL_COMPRESS_ERROR | MEL_COMPRESS_NO_MEMORY;
        return NULL;
    }
    memset(z, 0, sizeof *z);
    z->alloc = begin.alloc;
    z->decompress = begin.decompress;
    if (begin.decompress)
        z->magic_left = RLE_MAGIC_LEN;
    else
    {
        z->outbox = mel_alloc(begin.alloc, RLE_OUTBOX_CAP);
        z->lit = mel_alloc(begin.alloc, RLE_MAX_RUN);
        if (!z->outbox || !z->lit)
        {
            if (z->outbox)
                mel_dealloc(begin.alloc, z->outbox);
            if (z->lit)
                mel_dealloc(begin.alloc, z->lit);
            mel_dealloc(begin.alloc, z);
            *status = MEL_COMPRESS_ERROR | MEL_COMPRESS_NO_MEMORY;
            return NULL;
        }
        memcpy(z->outbox, g_rle_magic, RLE_MAGIC_LEN);
        z->box_len = RLE_MAGIC_LEN;
    }
    *status = MEL_COMPRESS_OK;
    return (Mel_Compress_Stream*)z;
}

static void rle_end(Mel_Compress_Stream* s)
{
    Rle_Stream* z = (Rle_Stream*)s;
    if (!z)
        return;
    if (z->outbox)
        mel_dealloc(z->alloc, z->outbox);
    if (z->lit)
        mel_dealloc(z->alloc, z->lit);
    mel_dealloc(z->alloc, z);
}

static bool rle_sniff(str8 head) { return head.len >= RLE_MAGIC_LEN && memcmp(head.data, g_rle_magic, RLE_MAGIC_LEN) == 0; }

static usize rle_bound(usize src_len, u32 level)
{
    (void)level;
    return src_len + src_len / RLE_MAX_RUN + 16;
}

static const Mel_Compress_Codec g_rle_codec = {
    .id = { (u8*)"rle", 3 },
    .ext = { (u8*)"rle", 3 },
    .level_min = 1,
    .level_max = 1,
    .level_default = 1,
    .sniff = rle_sniff,
    .bound = rle_bound,
    .begin = rle_begin,
    .step = rle_step,
    .end = rle_end,
};

const Mel_Compress_Codec* mel_compress_rle(void) { return &g_rle_codec; }
