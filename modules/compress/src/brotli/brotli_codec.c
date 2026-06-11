#include <compress/brotli.h>

#include <allocator/allocator.h>

#include <brotli/decode.h>
#include <brotli/encode.h>

#include <string.h>

typedef struct
{
    const Mel_Alloc*    alloc;
    bool                decompress;
    BrotliEncoderState* enc;
    BrotliDecoderState* dec;
} Brotli_Stream;

static void* brotli_alloc_bridge(void* opaque, size_t size) { return mel_alloc((const Mel_Alloc*)opaque, size); }

static void brotli_free_bridge(void* opaque, void* address)
{
    if (address)
        mel_dealloc((const Mel_Alloc*)opaque, address);
}

static Mel_Compress_Step brotli_step_compress(Brotli_Stream* z, str8 in, bool in_last, u8* out, usize out_cap)
{
    Mel_Compress_Step step = { 0 };

    size_t    avail_in = (size_t)in.len;
    const u8* next_in = in.data;
    size_t    avail_out = out_cap;
    u8*       next_out = out;

    for (;;)
    {
        BROTLI_BOOL ok = BrotliEncoderCompressStream(z->enc, in_last ? BROTLI_OPERATION_FINISH : BROTLI_OPERATION_PROCESS, &avail_in, &next_in, &avail_out, &next_out, NULL);
        if (!ok)
        {
            step.status = MEL_COMPRESS_ERROR | MEL_COMPRESS_BAD_STATE;
            break;
        }
        if (BrotliEncoderIsFinished(z->enc))
        {
            step.finished = true;
            break;
        }
        if (avail_out == 0)
        {
            step.status |= MEL_COMPRESS_OUTPUT_FULL;
            break;
        }
        if (avail_in == 0 && !BrotliEncoderHasMoreOutput(z->enc))
            break;
    }

    step.in_consumed = (usize)in.len - avail_in;
    step.out_produced = out_cap - avail_out;
    return step;
}

static Mel_Compress_Step brotli_step_decompress(Brotli_Stream* z, str8 in, bool in_last, u8* out, usize out_cap)
{
    Mel_Compress_Step step = { 0 };

    size_t    avail_in = (size_t)in.len;
    const u8* next_in = in.data;
    size_t    avail_out = out_cap;
    u8*       next_out = out;

    BrotliDecoderResult rc = BrotliDecoderDecompressStream(z->dec, &avail_in, &next_in, &avail_out, &next_out, NULL);

    step.in_consumed = (usize)in.len - avail_in;
    step.out_produced = out_cap - avail_out;

    if (rc == BROTLI_DECODER_RESULT_SUCCESS)
        step.finished = true;
    else if (rc == BROTLI_DECODER_RESULT_ERROR)
        step.status = MEL_COMPRESS_ERROR | MEL_COMPRESS_CORRUPT;
    else if (rc == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT)
        step.status |= MEL_COMPRESS_OUTPUT_FULL;
    else if (in_last && step.in_consumed == (usize)in.len)
        step.status = MEL_COMPRESS_ERROR | MEL_COMPRESS_TRUNCATED;
    return step;
}

static Mel_Compress_Step brotli_step(Mel_Compress_Stream* s, str8 in, bool in_last, u8* out, usize out_cap)
{
    Brotli_Stream* z = (Brotli_Stream*)s;
    if (z->decompress)
        return brotli_step_decompress(z, in, in_last, out, out_cap);
    return brotli_step_compress(z, in, in_last, out, out_cap);
}

static Mel_Compress_Stream* brotli_begin(Mel_Compress_Begin begin, Mel_Compress_Status* status)
{
    if (!begin.alloc)
    {
        *status = MEL_COMPRESS_ERROR | MEL_COMPRESS_BAD_STATE;
        return NULL;
    }
    if (!begin.decompress && (begin.level < 1 || begin.level > BROTLI_MAX_QUALITY))
    {
        *status = MEL_COMPRESS_ERROR | MEL_COMPRESS_BAD_LEVEL;
        return NULL;
    }
    Brotli_Stream* z = mel_alloc_type(begin.alloc, Brotli_Stream);
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
        z->dec = BrotliDecoderCreateInstance(brotli_alloc_bridge, brotli_free_bridge, (void*)begin.alloc);
        if (!z->dec)
        {
            mel_dealloc(begin.alloc, z);
            *status = MEL_COMPRESS_ERROR | MEL_COMPRESS_NO_MEMORY;
            return NULL;
        }
    }
    else
    {
        z->enc = BrotliEncoderCreateInstance(brotli_alloc_bridge, brotli_free_bridge, (void*)begin.alloc);
        if (!z->enc)
        {
            mel_dealloc(begin.alloc, z);
            *status = MEL_COMPRESS_ERROR | MEL_COMPRESS_NO_MEMORY;
            return NULL;
        }
        BrotliEncoderSetParameter(z->enc, BROTLI_PARAM_QUALITY, begin.level);
    }
    *status = MEL_COMPRESS_OK;
    return (Mel_Compress_Stream*)z;
}

static void brotli_end(Mel_Compress_Stream* s)
{
    Brotli_Stream* z = (Brotli_Stream*)s;
    if (!z)
        return;
    if (z->enc)
        BrotliEncoderDestroyInstance(z->enc);
    if (z->dec)
        BrotliDecoderDestroyInstance(z->dec);
    mel_dealloc(z->alloc, z);
}

static usize brotli_bound(usize src_len, u32 level)
{
    (void)level;
    usize n = BrotliEncoderMaxCompressedSize(src_len);
    return n ? n + 16 : src_len + src_len / 2 + 64;
}

static const Mel_Compress_Codec g_brotli_codec = {
    .id = { (u8*)"brotli", 6 },
    .ext = { (u8*)"br", 2 },
    .level_min = 1,
    .level_max = 11,
    .level_default = 11,
    .sniff = NULL,
    .bound = brotli_bound,
    .begin = brotli_begin,
    .step = brotli_step,
    .end = brotli_end,
};

const Mel_Compress_Codec* mel_compress_brotli(void) { return &g_brotli_codec; }
