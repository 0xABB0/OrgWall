#include <compress/lz4.h>

#include <allocator/allocator.h>

#define LZ4F_STATIC_LINKING_ONLY
#include <lz4frame.h>

#include <string.h>

#define LZ4_CHUNK_MAX ((usize)64 * 1024)

typedef struct
{
    const Mel_Alloc*   alloc;
    bool               decompress;
    LZ4F_cctx*         cctx;
    LZ4F_dctx*         dctx;
    LZ4F_preferences_t prefs;
    bool               header_done;
    bool               ended;
} Lz4_Stream;

static void* lz4_alloc_bridge(void* opaque, size_t size) { return mel_alloc((const Mel_Alloc*)opaque, size); }

static void* lz4_calloc_bridge(void* opaque, size_t size) { return mel_calloc((const Mel_Alloc*)opaque, size); }

static void lz4_free_bridge(void* opaque, void* address) { mel_dealloc((const Mel_Alloc*)opaque, address); }

static LZ4F_CustomMem lz4_cmem(const Mel_Alloc* alloc)
{
    LZ4F_CustomMem cm = { .customAlloc = lz4_alloc_bridge, .customCalloc = lz4_calloc_bridge, .customFree = lz4_free_bridge, .opaqueState = (void*)alloc };
    return cm;
}

static Mel_Compress_Step lz4_step_compress(Lz4_Stream* z, str8 in, bool in_last, u8* out, usize out_cap)
{
    Mel_Compress_Step step = { 0 };

    if (!z->header_done)
    {
        if (out_cap < LZ4F_HEADER_SIZE_MAX)
        {
            step.status = MEL_COMPRESS_OK | MEL_COMPRESS_OUTPUT_FULL;
            return step;
        }
        size_t n = LZ4F_compressBegin(z->cctx, out, out_cap, &z->prefs);
        if (LZ4F_isError(n))
        {
            step.status = MEL_COMPRESS_ERROR | MEL_COMPRESS_BAD_STATE;
            return step;
        }
        step.out_produced += n;
        z->header_done = true;
    }

    while (step.in_consumed < (usize)in.len)
    {
        usize room = out_cap - step.out_produced;
        usize chunk = (usize)in.len - step.in_consumed;
        if (chunk > LZ4_CHUNK_MAX)
            chunk = LZ4_CHUNK_MAX;
        while (chunk > 0 && LZ4F_compressBound(chunk, &z->prefs) > room)
            chunk /= 2;
        if (chunk == 0)
        {
            step.status |= MEL_COMPRESS_OUTPUT_FULL;
            return step;
        }
        size_t n = LZ4F_compressUpdate(z->cctx, out + step.out_produced, room, in.data + step.in_consumed, chunk, NULL);
        if (LZ4F_isError(n))
        {
            step.status = MEL_COMPRESS_ERROR | MEL_COMPRESS_BAD_STATE;
            return step;
        }
        step.out_produced += n;
        step.in_consumed += chunk;
    }

    if (in_last && !z->ended)
    {
        usize room = out_cap - step.out_produced;
        if (LZ4F_compressBound(0, &z->prefs) > room)
        {
            step.status |= MEL_COMPRESS_OUTPUT_FULL;
            return step;
        }
        size_t n = LZ4F_compressEnd(z->cctx, out + step.out_produced, room, NULL);
        if (LZ4F_isError(n))
        {
            step.status = MEL_COMPRESS_ERROR | MEL_COMPRESS_BAD_STATE;
            return step;
        }
        step.out_produced += n;
        z->ended = true;
    }

    step.finished = z->ended;
    return step;
}

static Mel_Compress_Step lz4_step_decompress(Lz4_Stream* z, str8 in, bool in_last, u8* out, usize out_cap)
{
    Mel_Compress_Step step = { 0 };

    if (z->ended)
    {
        step.finished = true;
        return step;
    }

    for (;;)
    {
        size_t dst_size = out_cap - step.out_produced;
        size_t src_size = (usize)in.len - step.in_consumed;
        size_t hint = LZ4F_decompress(z->dctx, out + step.out_produced, &dst_size, in.data + step.in_consumed, &src_size, NULL);
        if (LZ4F_isError(hint))
        {
            step.status = MEL_COMPRESS_ERROR | MEL_COMPRESS_CORRUPT;
            return step;
        }
        step.in_consumed += src_size;
        step.out_produced += dst_size;
        if (hint == 0)
        {
            z->ended = true;
            step.finished = true;
            return step;
        }
        if (step.out_produced == out_cap)
        {
            step.status |= MEL_COMPRESS_OUTPUT_FULL;
            return step;
        }
        if (step.in_consumed == (usize)in.len)
        {
            if (in_last)
                step.status = MEL_COMPRESS_ERROR | MEL_COMPRESS_TRUNCATED;
            return step;
        }
        if (src_size == 0 && dst_size == 0)
        {
            step.status = MEL_COMPRESS_ERROR | MEL_COMPRESS_BAD_STATE;
            return step;
        }
    }
}

static Mel_Compress_Step lz4_step(Mel_Compress_Stream* s, str8 in, bool in_last, u8* out, usize out_cap)
{
    Lz4_Stream* z = (Lz4_Stream*)s;
    if (z->decompress)
        return lz4_step_decompress(z, in, in_last, out, out_cap);
    return lz4_step_compress(z, in, in_last, out, out_cap);
}

static Mel_Compress_Stream* lz4_begin(Mel_Compress_Begin begin, Mel_Compress_Status* status)
{
    if (!begin.alloc)
    {
        *status = MEL_COMPRESS_ERROR | MEL_COMPRESS_BAD_STATE;
        return NULL;
    }
    if (!begin.decompress && (begin.level < 1 || begin.level > (u32)LZ4F_compressionLevel_max()))
    {
        *status = MEL_COMPRESS_ERROR | MEL_COMPRESS_BAD_LEVEL;
        return NULL;
    }
    Lz4_Stream* z = mel_alloc_type(begin.alloc, Lz4_Stream);
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
        z->dctx = LZ4F_createDecompressionContext_advanced(lz4_cmem(begin.alloc), LZ4F_VERSION);
        if (!z->dctx)
        {
            mel_dealloc(begin.alloc, z);
            *status = MEL_COMPRESS_ERROR | MEL_COMPRESS_NO_MEMORY;
            return NULL;
        }
    }
    else
    {
        z->prefs.compressionLevel = (int)begin.level;
        z->cctx = LZ4F_createCompressionContext_advanced(lz4_cmem(begin.alloc), LZ4F_VERSION);
        if (!z->cctx)
        {
            mel_dealloc(begin.alloc, z);
            *status = MEL_COMPRESS_ERROR | MEL_COMPRESS_NO_MEMORY;
            return NULL;
        }
    }
    *status = MEL_COMPRESS_OK;
    return (Mel_Compress_Stream*)z;
}

static void lz4_end(Mel_Compress_Stream* s)
{
    Lz4_Stream* z = (Lz4_Stream*)s;
    if (!z)
        return;
    if (z->cctx)
        LZ4F_freeCompressionContext(z->cctx);
    if (z->dctx)
        LZ4F_freeDecompressionContext(z->dctx);
    mel_dealloc(z->alloc, z);
}

static usize lz4_bound(usize src_len, u32 level)
{
    LZ4F_preferences_t prefs = { 0 };
    prefs.compressionLevel = (int)level;
    return LZ4F_compressFrameBound(src_len, &prefs) + 16;
}

static bool lz4_sniff(str8 head) { return head.len >= 4 && head.data[0] == 0x04 && head.data[1] == 0x22 && head.data[2] == 0x4D && head.data[3] == 0x18; }

static const Mel_Compress_Codec g_lz4_codec = {
    .id = { (u8*)"lz4", 3 },
    .ext = { (u8*)"lz4", 3 },
    .level_min = 1,
    .level_max = 12,
    .level_default = 1,
    .sniff = lz4_sniff,
    .bound = lz4_bound,
    .begin = lz4_begin,
    .step = lz4_step,
    .end = lz4_end,
};

const Mel_Compress_Codec* mel_compress_lz4(void) { return &g_lz4_codec; }
