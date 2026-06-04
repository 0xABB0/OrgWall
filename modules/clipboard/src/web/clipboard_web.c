#include <clipboard/backend.h>
#include <log/log.h>

#include <emscripten.h>
#include <stdlib.h>

EM_JS(int, mel_clip_js_available, (void), { return (typeof navigator != "undefined" && navigator.clipboard) ? 1 : 0; })

EM_JS(void, mel_clip_js_write_text, (unsigned lo, unsigned hi, const char* ptr, int len), {
    var s = ptr ? UTF8ToString(ptr, len) : "";
    if (navigator.clipboard && navigator.clipboard.writeText)
        navigator.clipboard.writeText(s).then(function() { _mel_clip_web__on_write(lo, hi, 1); }).catch(function() { _mel_clip_web__on_write(lo, hi, 0); });
    else
        _mel_clip_web__on_write(lo, hi, 0);
})

EM_JS(void, mel_clip_js_read_text, (unsigned lo, unsigned hi), {
    if (navigator.clipboard && navigator.clipboard.readText)
        navigator.clipboard.readText()
            .then(function(s) {
                var len = lengthBytesUTF8(s);
                var p = _malloc(len + 1);
                stringToUTF8(s, p, len + 1);
                _mel_clip_web__on_text(lo, hi, p, len, 1);
            })
            .catch(function() { _mel_clip_web__on_text(lo, hi, 0, 0, 0); });
    else
        _mel_clip_web__on_text(lo, hi, 0, 0, 0);
})

EMSCRIPTEN_KEEPALIVE void mel_clip_web__on_write(unsigned lo, unsigned hi, int ok)
{
    Mel_Clip_Job* j = mel_clip__job_from_token(((u64)hi << 32) | (u64)lo);
    if (j)
        mel_clip_job_resolve(j, ok ? MEL_CLIP_OK : (MEL_CLIP_ERROR | MEL_CLIP_RESULT_DENIED));
}

EMSCRIPTEN_KEEPALIVE void mel_clip_web__on_text(unsigned lo, unsigned hi, char* ptr, int len, int ok)
{
    Mel_Clip_Job* j = mel_clip__job_from_token(((u64)hi << 32) | (u64)lo);
    if (j)
    {
        if (ok)
        {
            if (ptr && len > 0)
                mel_clip_job_emit(j, MEL_CLIP_FMT_TEXT, ptr, (usize)len);
            mel_clip_job_resolve(j, len > 0 ? MEL_CLIP_OK : MEL_CLIP_RESULT_EMPTY);
        }
        else
            mel_clip_job_resolve(j, MEL_CLIP_ERROR | MEL_CLIP_RESULT_DENIED);
    }
    if (ptr)
        free(ptr);
}

void mel_clip__plat_read(Mel_Clip_Job* job)
{
    if (!mel_clip_job_wants(job, MEL_CLIP_FMT_TEXT))
    {
        mel_clip_job_add_warning(job, MEL_CLIP_WARN_FORMAT_UNAVAILABLE);
        mel_clip_job_resolve(job, MEL_CLIP_RESULT_EMPTY | MEL_CLIP_WARNED);
        return;
    }
    u64 tok = mel_clip_job_token(job);
    mel_clip_js_read_text((unsigned)tok, (unsigned)(tok >> 32));
}

void mel_clip__plat_write(Mel_Clip_Job* job)
{
    str8 text = STR8_EMPTY;
    bool other = false;
    u32  reps = mel_clip_job_rep_count(job, 0);
    for (u32 r = 0; r < reps; r++)
    {
        Mel_Clip_Rep rep = mel_clip_job_rep(job, 0, r);
        if (rep.format == MEL_CLIP_FMT_TEXT)
            text = rep.bytes;
        else
            other = true;
    }
    if (other || mel_clip_job_item_count(job) > 1)
        mel_clip_job_add_warning(job, MEL_CLIP_WARN_REPRESENTATION_DROPPED);
    u64 tok = mel_clip_job_token(job);
    mel_clip_js_write_text((unsigned)tok, (unsigned)(tok >> 32), (const char*)text.data, (int)text.len);
}

void mel_clip__plat_clear(Mel_Clip_Job* job)
{
    u64 tok = mel_clip_job_token(job);
    mel_clip_js_write_text((unsigned)tok, (unsigned)(tok >> 32), "", 0);
}

void mel_clip__plat_query(Mel_Clip_Job* job)
{
    mel_log_warn("clipboard", "web backend: format enumeration unsupported; use read");
    mel_clip_job_resolve(job, MEL_CLIP_WARNED);
}

void mel_clip__plat_has(Mel_Clip_Job* job)
{
    mel_log_warn("clipboard", "web backend: synchronous presence check unsupported; use read");
    mel_clip_job_set_present(job, false);
    mel_clip_job_resolve(job, MEL_CLIP_WARNED | MEL_CLIP_WARN_FORMAT_UNAVAILABLE);
}

bool mel_clip__plat_available(void) { return mel_clip_js_available() != 0; }

bool mel_clip__plat_channel_supported(Mel_Clip_Channel ch) { return mel_clip_channel_resolve(ch) == (Mel_Clip_Channel)MEL_CLIP_CHANNEL_CLIPBOARD; }

u64 mel_clip__plat_sequence(Mel_Clip_Channel ch)
{
    (void)ch;
    return 0;
}

void* mel_clip__plat_native(void) { return NULL; }
