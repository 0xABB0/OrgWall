#include <compress/compress.h>

#include <allocator/allocator.h>

static Mel_Compress_Result run_pump(const Mel_Compress_Codec* codec, str8 in, Mel_Compress_Begin begin, usize initial_cap)
{
    Mel_Compress_Result r = { 0 };
    if (!codec || !begin.alloc)
    {
        r.status = MEL_COMPRESS_ERROR | MEL_COMPRESS_BAD_STATE;
        return r;
    }

    Mel_Compress_Status  st = MEL_COMPRESS_OK;
    Mel_Compress_Stream* s = codec->begin(begin, &st);
    if (!s)
    {
        r.status = st;
        return r;
    }

    usize cap = initial_cap < 64 ? 64 : initial_cap;
    u8*   out = mel_alloc(begin.alloc, cap);
    if (!out)
    {
        codec->end(s);
        r.status = MEL_COMPRESS_ERROR | MEL_COMPRESS_NO_MEMORY;
        return r;
    }

    usize len = 0;
    str8  rest = in;
    for (;;)
    {
        Mel_Compress_Step step = codec->step(s, rest, true, out + len, cap - len);
        len += step.out_produced;
        rest = str8_slice(rest, (size)step.in_consumed, rest.len - (size)step.in_consumed);

        if (mel_compress_status_failed(step.status))
        {
            codec->end(s);
            mel_dealloc(begin.alloc, out);
            r.status = step.status;
            return r;
        }
        if (step.finished)
            break;

        bool stalled = step.in_consumed == 0 && step.out_produced == 0;
        bool full = (step.status & MEL_COMPRESS_OUTPUT_FULL) != 0 || cap - len < 64;
        if (full || stalled)
        {
            usize new_cap = cap * 2;
            u8*   grown = mel_realloc(begin.alloc, out, new_cap);
            if (!grown)
            {
                codec->end(s);
                mel_dealloc(begin.alloc, out);
                r.status = MEL_COMPRESS_ERROR | MEL_COMPRESS_NO_MEMORY;
                return r;
            }
            out = grown;
            cap = new_cap;
        }
    }

    codec->end(s);
    r.data = out;
    r.len = len;
    r.status = MEL_COMPRESS_OK;
    return r;
}

Mel_Compress_Result mel_compress_opt(const Mel_Compress_Codec* codec, str8 in, Mel_Compress_Opt opt)
{
    Mel_Compress_Begin begin = { .decompress = false, .level = opt.level, .alloc = opt.alloc };
    usize              cap = codec ? codec->bound(in.len, opt.level) : 0;
    return run_pump(codec, in, begin, cap);
}

Mel_Compress_Result mel_decompress_opt(const Mel_Compress_Codec* codec, str8 in, Mel_Decompress_Opt opt)
{
    Mel_Compress_Begin begin = { .decompress = true, .alloc = opt.alloc };
    usize              cap = in.len > (usize)(1u << 20) ? in.len * 2 : in.len * 4;
    return run_pump(codec, in, begin, cap);
}

void mel_compress_result_free(Mel_Compress_Result* r, const Mel_Alloc* alloc)
{
    if (!r || !r->data)
        return;
    mel_dealloc(alloc, r->data);
    r->data = NULL;
    r->len = 0;
}
