#include <compress/zstd.h>

#include <allocator/allocator.h>

#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>

#include <string.h>

typedef struct
{
    const Mel_Alloc* alloc;
    bool             decompress;
    ZSTD_CCtx*       cctx;
    ZSTD_DCtx*       dctx;
} Zstd_Stream;

static void* zstd_alloc_bridge(void* opaque, size_t size) { return mel_alloc((const Mel_Alloc*)opaque, size); }

static void zstd_free_bridge(void* opaque, void* address) { mel_dealloc((const Mel_Alloc*)opaque, address); }

static ZSTD_customMem zstd_cmem(const Mel_Alloc* alloc)
{
    ZSTD_customMem cm = { .customAlloc = zstd_alloc_bridge, .customFree = zstd_free_bridge, .opaque = (void*)alloc };
    return cm;
}

static Mel_Compress_Status map_zstd_error(size_t code, bool decompress)
{
    if (ZSTD_getErrorCode(code) == ZSTD_error_memory_allocation)
        return MEL_COMPRESS_ERROR | MEL_COMPRESS_NO_MEMORY;
    if (decompress)
        return MEL_COMPRESS_ERROR | MEL_COMPRESS_CORRUPT;
    return MEL_COMPRESS_ERROR | MEL_COMPRESS_BAD_STATE;
}

static Mel_Compress_Step zstd_step(Mel_Compress_Stream* s, str8 in, bool in_last, u8* out, usize out_cap)
{
    Zstd_Stream*      z = (Zstd_Stream*)s;
    Mel_Compress_Step step = { 0 };

    ZSTD_inBuffer  ib = { in.data, (size_t)in.len, 0 };
    ZSTD_outBuffer ob = { out, out_cap, 0 };

    for (;;)
    {
        size_t prev_in = ib.pos;
        size_t prev_out = ob.pos;
        size_t rem;
        if (z->decompress)
            rem = ZSTD_decompressStream(z->dctx, &ob, &ib);
        else
            rem = ZSTD_compressStream2(z->cctx, &ob, &ib, in_last ? ZSTD_e_end : ZSTD_e_continue);
        if (ZSTD_isError(rem))
        {
            step.in_consumed = ib.pos;
            step.out_produced = ob.pos;
            step.status = map_zstd_error(rem, z->decompress);
            return step;
        }
        bool done = z->decompress ? rem == 0 : (in_last && rem == 0 && ib.pos == ib.size);
        if (done)
        {
            step.in_consumed = ib.pos;
            step.out_produced = ob.pos;
            step.finished = true;
            return step;
        }
        bool progress = ib.pos > prev_in || ob.pos > prev_out;
        if (!progress || ob.pos == ob.size || ib.pos == ib.size)
        {
            step.in_consumed = ib.pos;
            step.out_produced = ob.pos;
            if (ob.pos == ob.size)
                step.status |= MEL_COMPRESS_OUTPUT_FULL;
            else if (z->decompress && in_last && ib.pos == ib.size)
                step.status = MEL_COMPRESS_ERROR | MEL_COMPRESS_TRUNCATED;
            return step;
        }
    }
}

static Mel_Compress_Stream* zstd_begin(Mel_Compress_Begin begin, Mel_Compress_Status* status)
{
    if (!begin.alloc)
    {
        *status = MEL_COMPRESS_ERROR | MEL_COMPRESS_BAD_STATE;
        return NULL;
    }
    if (!begin.decompress && (begin.level < 1 || begin.level > (u32)ZSTD_maxCLevel()))
    {
        *status = MEL_COMPRESS_ERROR | MEL_COMPRESS_BAD_LEVEL;
        return NULL;
    }
    Zstd_Stream* z = mel_alloc_type(begin.alloc, Zstd_Stream);
    if (!z)
    {
        *status = MEL_COMPRESS_ERROR | MEL_COMPRESS_NO_MEMORY;
        return NULL;
    }
    memset(z, 0, sizeof *z);
    z->alloc = begin.alloc;
    z->decompress = begin.decompress;
    if (begin.decompress)
    {
        z->dctx = ZSTD_createDCtx_advanced(zstd_cmem(begin.alloc));
        if (!z->dctx)
        {
            mel_dealloc(begin.alloc, z);
            *status = MEL_COMPRESS_ERROR | MEL_COMPRESS_NO_MEMORY;
            return NULL;
        }
    }
    else
    {
        z->cctx = ZSTD_createCCtx_advanced(zstd_cmem(begin.alloc));
        if (!z->cctx)
        {
            mel_dealloc(begin.alloc, z);
            *status = MEL_COMPRESS_ERROR | MEL_COMPRESS_NO_MEMORY;
            return NULL;
        }
        ZSTD_CCtx_setParameter(z->cctx, ZSTD_c_compressionLevel, (int)begin.level);
    }
    *status = MEL_COMPRESS_OK;
    return (Mel_Compress_Stream*)z;
}

static void zstd_end(Mel_Compress_Stream* s)
{
    Zstd_Stream* z = (Zstd_Stream*)s;
    if (!z)
        return;
    if (z->cctx)
        ZSTD_freeCCtx(z->cctx);
    if (z->dctx)
        ZSTD_freeDCtx(z->dctx);
    mel_dealloc(z->alloc, z);
}

static usize zstd_bound(usize src_len, u32 level)
{
    (void)level;
    return ZSTD_compressBound(src_len) + 16;
}

static bool zstd_sniff(str8 head) { return head.len >= 4 && head.data[0] == 0x28 && head.data[1] == 0xB5 && head.data[2] == 0x2F && head.data[3] == 0xFD; }

static const Mel_Compress_Codec g_zstd_codec = {
    .id = { (u8*)"zstd", 4 },
    .ext = { (u8*)"zst", 3 },
    .level_min = 1,
    .level_max = 22,
    .level_default = 3,
    .sniff = zstd_sniff,
    .bound = zstd_bound,
    .begin = zstd_begin,
    .step = zstd_step,
    .end = zstd_end,
};

const Mel_Compress_Codec* mel_compress_zstd(void) { return &g_zstd_codec; }
