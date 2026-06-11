#include <compress/deflate.h>

#include <allocator/allocator.h>

#include <miniz.h>

#include <string.h>

#define DEFLATE_CHUNK_MAX ((usize)1 << 28)

static const u8 g_gzip_header[10] = { 0x1F, 0x8B, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF };

typedef struct
{
    const Mel_Alloc* alloc;
    bool             decompress;
    bool             gzip;

    mz_stream zs;
    bool      zs_live;
    bool      z_end;

    u32 crc;
    u64 raw_len;

    usize hdr_left;
    bool  trl_ready;
    usize trl_left;
    u8    trl[8];

    bool  ghdr_done;
    usize ghdr_fixed_left;
    u8    gflg;
    usize extra_len_left;
    usize extra_len_lo;
    usize extra_left;
    bool  name_pending;
    bool  comment_pending;
    usize hcrc_left;
    usize gtrl_have;
    u8    gtrl[8];
} Deflate_Stream;

static void* mz_alloc_bridge(void* opaque, size_t items, size_t size) { return mel_alloc((const Mel_Alloc*)opaque, items * size); }

static void mz_free_bridge(void* opaque, void* address) { mel_dealloc((const Mel_Alloc*)opaque, address); }

static Mel_Compress_Status map_mz_error(int rc, bool out_full, bool in_last)
{
    if (rc == MZ_DATA_ERROR)
        return MEL_COMPRESS_ERROR | MEL_COMPRESS_CORRUPT;
    if (rc == MZ_MEM_ERROR)
        return MEL_COMPRESS_ERROR | MEL_COMPRESS_NO_MEMORY;
    if (rc == MZ_BUF_ERROR)
    {
        if (out_full)
            return MEL_COMPRESS_OK | MEL_COMPRESS_OUTPUT_FULL;
        if (in_last)
            return MEL_COMPRESS_ERROR | MEL_COMPRESS_TRUNCATED;
        return MEL_COMPRESS_OK;
    }
    if (rc < 0)
        return MEL_COMPRESS_ERROR | MEL_COMPRESS_BAD_STATE;
    return MEL_COMPRESS_OK;
}

static void pump_z(Deflate_Stream* z, str8 in, bool in_last, u8* out, usize out_cap, Mel_Compress_Step* step, bool* made_progress)
{
    usize in_left = (usize)in.len - step->in_consumed;
    usize out_left = out_cap - step->out_produced;
    usize in_chunk = in_left < DEFLATE_CHUNK_MAX ? in_left : DEFLATE_CHUNK_MAX;
    usize out_chunk = out_left < DEFLATE_CHUNK_MAX ? out_left : DEFLATE_CHUNK_MAX;

    z->zs.next_in = in.data + step->in_consumed;
    z->zs.avail_in = (unsigned int)in_chunk;
    z->zs.next_out = out + step->out_produced;
    z->zs.avail_out = (unsigned int)out_chunk;

    int rc;
    if (z->decompress)
        rc = mz_inflate(&z->zs, MZ_SYNC_FLUSH);
    else
        rc = mz_deflate(&z->zs, in_last && in_chunk == in_left ? MZ_FINISH : MZ_NO_FLUSH);

    usize consumed = in_chunk - z->zs.avail_in;
    usize produced = out_chunk - z->zs.avail_out;
    if (z->gzip)
    {
        if (z->decompress)
            z->crc = (u32)mz_crc32(z->crc, out + step->out_produced, produced);
        else
            z->crc = (u32)mz_crc32(z->crc, in.data + step->in_consumed, consumed);
    }
    z->raw_len += z->decompress ? produced : consumed;
    step->in_consumed += consumed;
    step->out_produced += produced;
    *made_progress = consumed > 0 || produced > 0;

    if (rc == MZ_STREAM_END)
    {
        z->z_end = true;
        return;
    }
    Mel_Compress_Status st = map_mz_error(rc, z->zs.avail_out == 0, in_last && step->in_consumed == (usize)in.len);
    step->status |= st;
}

static Mel_Compress_Step deflate_step_compress(Deflate_Stream* z, str8 in, bool in_last, u8* out, usize out_cap)
{
    Mel_Compress_Step step = { 0 };

    while (z->hdr_left > 0 && step.out_produced < out_cap)
    {
        out[step.out_produced++] = g_gzip_header[10 - z->hdr_left];
        z->hdr_left--;
    }
    if (z->hdr_left > 0)
    {
        step.status |= MEL_COMPRESS_OUTPUT_FULL;
        return step;
    }

    while (!z->z_end)
    {
        bool progress = false;
        pump_z(z, in, in_last, out, out_cap, &step, &progress);
        if (mel_compress_status_failed(step.status))
            return step;
        if (z->z_end)
            break;
        if (!progress || step.out_produced == out_cap)
            return step;
        if (step.in_consumed == (usize)in.len && !in_last)
            return step;
    }

    if (z->gzip)
    {
        if (!z->trl_ready)
        {
            z->trl_ready = true;
            u32 isize = (u32)z->raw_len;
            z->trl[0] = (u8)(z->crc);
            z->trl[1] = (u8)(z->crc >> 8);
            z->trl[2] = (u8)(z->crc >> 16);
            z->trl[3] = (u8)(z->crc >> 24);
            z->trl[4] = (u8)(isize);
            z->trl[5] = (u8)(isize >> 8);
            z->trl[6] = (u8)(isize >> 16);
            z->trl[7] = (u8)(isize >> 24);
            z->trl_left = 8;
        }
        while (z->trl_left > 0 && step.out_produced < out_cap)
        {
            out[step.out_produced++] = z->trl[8 - z->trl_left];
            z->trl_left--;
        }
        if (z->trl_left > 0)
        {
            step.status |= MEL_COMPRESS_OUTPUT_FULL;
            return step;
        }
    }

    step.finished = true;
    return step;
}

static bool gzip_parse_header(Deflate_Stream* z, str8 in, Mel_Compress_Step* step)
{
    while (!z->ghdr_done)
    {
        if (step->in_consumed >= (usize)in.len)
            return false;
        u8 c = in.data[step->in_consumed];
        if (z->ghdr_fixed_left > 0)
        {
            usize idx = 10 - z->ghdr_fixed_left;
            if ((idx == 0 && c != 0x1F) || (idx == 1 && c != 0x8B) || (idx == 2 && c != 0x08))
            {
                step->status = MEL_COMPRESS_ERROR | MEL_COMPRESS_UNKNOWN_FORMAT;
                return false;
            }
            if (idx == 3)
            {
                z->gflg = c;
                z->extra_len_left = (c & 0x04) ? 2 : 0;
                z->name_pending = (c & 0x08) != 0;
                z->comment_pending = (c & 0x10) != 0;
                z->hcrc_left = (c & 0x02) ? 2 : 0;
            }
            z->ghdr_fixed_left--;
            step->in_consumed++;
            continue;
        }
        if (z->extra_len_left > 0)
        {
            if (z->extra_len_left == 2)
                z->extra_len_lo = c;
            else
                z->extra_left = z->extra_len_lo | ((usize)c << 8);
            z->extra_len_left--;
            step->in_consumed++;
            continue;
        }
        if (z->extra_left > 0)
        {
            usize avail = (usize)in.len - step->in_consumed;
            usize n = z->extra_left < avail ? z->extra_left : avail;
            z->extra_left -= n;
            step->in_consumed += n;
            continue;
        }
        if (z->name_pending)
        {
            step->in_consumed++;
            if (c == 0)
                z->name_pending = false;
            continue;
        }
        if (z->comment_pending)
        {
            step->in_consumed++;
            if (c == 0)
                z->comment_pending = false;
            continue;
        }
        if (z->hcrc_left > 0)
        {
            z->hcrc_left--;
            step->in_consumed++;
            continue;
        }
        z->ghdr_done = true;
    }
    return true;
}

static Mel_Compress_Step deflate_step_decompress(Deflate_Stream* z, str8 in, bool in_last, u8* out, usize out_cap)
{
    Mel_Compress_Step step = { 0 };

    if (z->gzip && !z->ghdr_done)
    {
        if (!gzip_parse_header(z, in, &step))
        {
            if (!mel_compress_status_failed(step.status) && in_last)
                step.status = MEL_COMPRESS_ERROR | MEL_COMPRESS_TRUNCATED;
            return step;
        }
    }

    while (!z->z_end)
    {
        bool progress = false;
        pump_z(z, in, in_last, out, out_cap, &step, &progress);
        if (mel_compress_status_failed(step.status))
            return step;
        if (z->z_end)
            break;
        if (!progress)
            return step;
        if (step.out_produced == out_cap)
        {
            step.status |= MEL_COMPRESS_OUTPUT_FULL;
            return step;
        }
        if (step.in_consumed == (usize)in.len)
        {
            if (in_last && !(step.status & MEL_COMPRESS_OUTPUT_FULL))
                step.status = MEL_COMPRESS_ERROR | MEL_COMPRESS_TRUNCATED;
            return step;
        }
    }

    if (z->gzip)
    {
        while (z->gtrl_have < 8 && step.in_consumed < (usize)in.len)
            z->gtrl[z->gtrl_have++] = in.data[step.in_consumed++];
        if (z->gtrl_have < 8)
        {
            if (in_last)
                step.status = MEL_COMPRESS_ERROR | MEL_COMPRESS_TRUNCATED;
            return step;
        }
        u32 want_crc = (u32)z->gtrl[0] | ((u32)z->gtrl[1] << 8) | ((u32)z->gtrl[2] << 16) | ((u32)z->gtrl[3] << 24);
        u32 want_len = (u32)z->gtrl[4] | ((u32)z->gtrl[5] << 8) | ((u32)z->gtrl[6] << 16) | ((u32)z->gtrl[7] << 24);
        if (want_crc != z->crc || want_len != (u32)z->raw_len)
        {
            step.status = MEL_COMPRESS_ERROR | MEL_COMPRESS_CORRUPT;
            return step;
        }
    }

    step.finished = true;
    return step;
}

static Mel_Compress_Step deflate_step(Mel_Compress_Stream* s, str8 in, bool in_last, u8* out, usize out_cap)
{
    Deflate_Stream* z = (Deflate_Stream*)s;
    if (z->decompress)
        return deflate_step_decompress(z, in, in_last, out, out_cap);
    return deflate_step_compress(z, in, in_last, out, out_cap);
}

static Mel_Compress_Stream* deflate_begin_impl(Mel_Compress_Begin begin, Mel_Compress_Status* status, bool gzip)
{
    if (!begin.alloc)
    {
        *status = MEL_COMPRESS_ERROR | MEL_COMPRESS_BAD_STATE;
        return NULL;
    }
    if (!begin.decompress && (begin.level < 1 || begin.level > 9))
    {
        *status = MEL_COMPRESS_ERROR | MEL_COMPRESS_BAD_LEVEL;
        return NULL;
    }
    Deflate_Stream* z = mel_alloc_type(begin.alloc, Deflate_Stream);
    if (!z)
    {
        *status = MEL_COMPRESS_ERROR | MEL_COMPRESS_NO_MEMORY;
        return NULL;
    }
    memset(z, 0, sizeof *z);
    z->alloc = begin.alloc;
    z->decompress = begin.decompress;
    z->gzip = gzip;
    z->crc = (u32)mz_crc32(0, NULL, 0);
    z->zs.zalloc = mz_alloc_bridge;
    z->zs.zfree = mz_free_bridge;
    z->zs.opaque = (void*)begin.alloc;

    int window_bits = gzip ? -MZ_DEFAULT_WINDOW_BITS : MZ_DEFAULT_WINDOW_BITS;
    int rc;
    if (begin.decompress)
        rc = mz_inflateInit2(&z->zs, window_bits);
    else
        rc = mz_deflateInit2(&z->zs, (int)begin.level, MZ_DEFLATED, window_bits, 9, MZ_DEFAULT_STRATEGY);
    if (rc != MZ_OK)
    {
        mel_dealloc(begin.alloc, z);
        *status = MEL_COMPRESS_ERROR | (rc == MZ_MEM_ERROR ? MEL_COMPRESS_NO_MEMORY : MEL_COMPRESS_BAD_STATE);
        return NULL;
    }
    z->zs_live = true;
    if (gzip)
    {
        if (begin.decompress)
            z->ghdr_fixed_left = 10;
        else
            z->hdr_left = 10;
    }
    *status = MEL_COMPRESS_OK;
    return (Mel_Compress_Stream*)z;
}

static Mel_Compress_Stream* deflate_begin(Mel_Compress_Begin begin, Mel_Compress_Status* status) { return deflate_begin_impl(begin, status, false); }

static Mel_Compress_Stream* gzip_begin(Mel_Compress_Begin begin, Mel_Compress_Status* status) { return deflate_begin_impl(begin, status, true); }

static void deflate_end(Mel_Compress_Stream* s)
{
    Deflate_Stream* z = (Deflate_Stream*)s;
    if (!z)
        return;
    if (z->zs_live)
    {
        if (z->decompress)
            mz_inflateEnd(&z->zs);
        else
            mz_deflateEnd(&z->zs);
    }
    mel_dealloc(z->alloc, z);
}

static usize deflate_bound(usize src_len, u32 level)
{
    (void)level;
    return (usize)mz_compressBound((mz_ulong)src_len) + 32;
}

static usize gzip_bound(usize src_len, u32 level) { return deflate_bound(src_len, level) + 18; }

static bool deflate_sniff(str8 head)
{
    if (head.len < 2)
        return false;
    u8 cmf = head.data[0];
    u8 flg = head.data[1];
    if ((cmf & 0x0F) != 8)
        return false;
    return (((u32)cmf << 8) | flg) % 31 == 0;
}

static bool gzip_sniff(str8 head) { return head.len >= 3 && head.data[0] == 0x1F && head.data[1] == 0x8B && head.data[2] == 0x08; }

static const Mel_Compress_Codec g_deflate_codec = {
    .id = { (u8*)"deflate", 7 },
    .ext = { (u8*)"zz", 2 },
    .level_min = 1,
    .level_max = 9,
    .level_default = 6,
    .sniff = deflate_sniff,
    .bound = deflate_bound,
    .begin = deflate_begin,
    .step = deflate_step,
    .end = deflate_end,
};

static const Mel_Compress_Codec g_gzip_codec = {
    .id = { (u8*)"gzip", 4 },
    .ext = { (u8*)"gz", 2 },
    .level_min = 1,
    .level_max = 9,
    .level_default = 6,
    .sniff = gzip_sniff,
    .bound = gzip_bound,
    .begin = gzip_begin,
    .step = deflate_step,
    .end = deflate_end,
};

const Mel_Compress_Codec* mel_compress_deflate(void) { return &g_deflate_codec; }

const Mel_Compress_Codec* mel_compress_gzip(void) { return &g_gzip_codec; }
