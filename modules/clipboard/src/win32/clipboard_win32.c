#include <clipboard/backend.h>
#include <log/log.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static bool win32_open(void)
{
    for (int i = 0; i < 8; i++)
    {
        if (OpenClipboard(NULL))
            return true;
        Sleep(2);
    }
    return false;
}

static UINT cf_for_format(Mel_Clip_Job* j, Mel_Clip_Format f)
{
    if (f == MEL_CLIP_FMT_TEXT)
        return CF_UNICODETEXT;
    str8 mime = mel_clip_format_mime(f);
    if (str8_is_empty(mime))
        return 0;
    const Mel_Alloc* a = mel_clip_job_alloc(j);
    char*            c = (char*)mel_alloc(a, (usize)mime.len + 1);
    if (!c)
        return 0;
    memcpy(c, mime.data, (usize)mime.len);
    c[mime.len] = 0;
    UINT cf = RegisterClipboardFormatA(c);
    mel_dealloc(a, c);
    return cf;
}

static void emit_text_utf16(Mel_Clip_Job* j, const wchar_t* w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 1)
    {
        mel_clip_job_emit(j, MEL_CLIP_FMT_TEXT, "", 0);
        return;
    }
    const Mel_Alloc* a = mel_clip_job_alloc(j);
    char*            buf = (char*)mel_alloc(a, (usize)n);
    if (!buf)
        return;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, n, NULL, NULL);
    mel_clip_job_emit(j, MEL_CLIP_FMT_TEXT, buf, (usize)(n - 1));
    mel_dealloc(a, buf);
}

void mel_clip__plat_read(Mel_Clip_Job* job)
{
    if (!win32_open())
    {
        mel_log_error("clipboard", "win32 OpenClipboard failed");
        mel_clip_job_resolve(job, MEL_CLIP_ERROR);
        return;
    }
    Mel_Clip_Status st = 0;
    u32             want = mel_clip_job_request_count(job);
    u32             emitted = 0;

    if (want > 0)
    {
        for (u32 i = 0; i < want; i++)
        {
            Mel_Clip_Format f = mel_clip_job_request(job, i);
            UINT            cf = cf_for_format(job, f);
            HANDLE          h = cf ? GetClipboardData(cf) : NULL;
            if (!h)
            {
                st |= MEL_CLIP_WARN_FORMAT_UNAVAILABLE;
                continue;
            }
            void* p = GlobalLock(h);
            if (p)
            {
                if (f == MEL_CLIP_FMT_TEXT)
                    emit_text_utf16(job, (const wchar_t*)p);
                else
                    mel_clip_job_emit(job, f, p, (usize)GlobalSize(h));
                emitted++;
                GlobalUnlock(h);
            }
        }
    }
    else
    {
        UINT cf = 0;
        while ((cf = EnumClipboardFormats(cf)) != 0)
        {
            Mel_Clip_Format f = MEL_CLIP_FMT_NONE;
            if (cf == CF_UNICODETEXT)
                f = MEL_CLIP_FMT_TEXT;
            else
            {
                char name[256];
                int  nl = GetClipboardFormatNameA(cf, name, (int)sizeof name);
                if (nl > 0)
                    f = mel_clip_format_register((str8){ (u8*)name, (size)nl });
            }
            if (f == MEL_CLIP_FMT_NONE)
                continue;
            HANDLE h = GetClipboardData(cf);
            void*  p = h ? GlobalLock(h) : NULL;
            if (p)
            {
                if (f == MEL_CLIP_FMT_TEXT)
                    emit_text_utf16(job, (const wchar_t*)p);
                else
                    mel_clip_job_emit(job, f, p, (usize)GlobalSize(h));
                emitted++;
                GlobalUnlock(h);
            }
        }
    }
    CloseClipboard();
    if (emitted == 0)
        st |= MEL_CLIP_RESULT_EMPTY;
    mel_clip_job_resolve(job, (st & ~MEL_CLIP_SEVERITY_MASK) ? (st | MEL_CLIP_WARNED) : MEL_CLIP_OK);
}

static HANDLE global_from_bytes(const void* p, usize n)
{
    HANDLE h = GlobalAlloc(GMEM_MOVEABLE, n);
    if (!h)
        return NULL;
    void* d = GlobalLock(h);
    if (!d)
    {
        GlobalFree(h);
        return NULL;
    }
    memcpy(d, p, n);
    GlobalUnlock(h);
    return h;
}

static HANDLE global_text_utf16(const char* utf8, int len)
{
    int    wn = MultiByteToWideChar(CP_UTF8, 0, utf8, len, NULL, 0);
    HANDLE h = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)(wn + 1) * sizeof(wchar_t));
    if (!h)
        return NULL;
    wchar_t* d = (wchar_t*)GlobalLock(h);
    if (!d)
    {
        GlobalFree(h);
        return NULL;
    }
    MultiByteToWideChar(CP_UTF8, 0, utf8, len, d, wn);
    d[wn] = 0;
    GlobalUnlock(h);
    return h;
}

void mel_clip__plat_write(Mel_Clip_Job* job)
{
    if (!win32_open())
    {
        mel_log_error("clipboard", "win32 OpenClipboard failed");
        mel_clip_job_resolve(job, MEL_CLIP_ERROR);
        return;
    }
    EmptyClipboard();
    if (mel_clip_job_item_count(job) > 1)
        mel_clip_job_add_warning(job, MEL_CLIP_WARN_REPRESENTATION_DROPPED);
    u32 reps = mel_clip_job_rep_count(job, 0);
    for (u32 r = 0; r < reps; r++)
    {
        Mel_Clip_Rep rep = mel_clip_job_rep(job, 0, r);
        if (rep.format == MEL_CLIP_FMT_HTML)
        {
            mel_clip_job_add_warning(job, MEL_CLIP_WARN_REPRESENTATION_DROPPED);
            continue;
        }
        HANDLE h;
        UINT   cf;
        if (rep.format == MEL_CLIP_FMT_TEXT)
        {
            cf = CF_UNICODETEXT;
            h = global_text_utf16((const char*)rep.bytes.data, (int)rep.bytes.len);
            mel_clip_job_add_warning(job, MEL_CLIP_WARN_TRANSCODED);
        }
        else
        {
            cf = cf_for_format(job, rep.format);
            h = (cf && rep.bytes.len) ? global_from_bytes(rep.bytes.data, (usize)rep.bytes.len) : NULL;
        }
        if (cf && h)
            SetClipboardData(cf, h);
        else
            mel_clip_job_add_warning(job, MEL_CLIP_WARN_REPRESENTATION_DROPPED);
    }
    CloseClipboard();
    mel_clip_job_resolve(job, MEL_CLIP_OK);
}

void mel_clip__plat_clear(Mel_Clip_Job* job)
{
    if (win32_open())
    {
        EmptyClipboard();
        CloseClipboard();
        mel_clip_job_resolve(job, MEL_CLIP_OK);
    }
    else
        mel_clip_job_resolve(job, MEL_CLIP_ERROR);
}

void mel_clip__plat_query(Mel_Clip_Job* job)
{
    if (!win32_open())
    {
        mel_clip_job_resolve(job, MEL_CLIP_ERROR);
        return;
    }
    UINT cf = 0;
    while ((cf = EnumClipboardFormats(cf)) != 0)
    {
        if (cf == CF_UNICODETEXT)
            mel_clip_job_emit_format(job, MEL_CLIP_FMT_TEXT);
        else
        {
            char name[256];
            int  nl = GetClipboardFormatNameA(cf, name, (int)sizeof name);
            if (nl > 0)
                mel_clip_job_emit_format(job, mel_clip_format_register((str8){ (u8*)name, (size)nl }));
        }
    }
    CloseClipboard();
    mel_clip_job_resolve(job, MEL_CLIP_OK);
}

u64 mel_clip__plat_sequence(void) { return (u64)GetClipboardSequenceNumber(); }

bool mel_clip__plat_available(void) { return true; }

void* mel_clip__plat_native(void) { return NULL; }
