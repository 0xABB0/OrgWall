#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <locale/provider.h>
#include <log/log.h>

#include <allocator/allocator.h>

#include <string.h>

static u32 wide_to_utf8(const WCHAR* w, int wlen, char* dst, int dst_cap)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, wlen, dst, dst_cap, NULL, NULL);
    return n > 0 ? (u32)n : 0;
}

static u32 win32_enumerate(void* user, const Mel_Alloc* alloc, Mel_Locale_Raw* out, u32 cap)
{
    (void)user;

    ULONG num = 0;
    ULONG chars = 0;
    if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &num, NULL, &chars) || chars == 0)
    {
        mel_log_error("locale", "GetUserPreferredUILanguages size query failed (err=%lu)", GetLastError());
        return 0;
    }

    WCHAR* block = (WCHAR*)mel_alloc(alloc, (usize)chars * sizeof(WCHAR));
    if (!block)
        return 0;
    if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &num, block, &chars))
    {
        mel_log_error("locale", "GetUserPreferredUILanguages fetch failed (err=%lu)", GetLastError());
        mel_dealloc(alloc, block);
        return 0;
    }

    if ((u32)num > cap)
    {
        mel_dealloc(alloc, block);
        return (u32)num;
    }

    u32          produced = 0;
    const WCHAR* p = block;
    while (*p && produced < cap)
    {
        int wlen = (int)wcslen(p);
        int u8cap = wlen * 4 + 1;
        u8* buf = (u8*)mel_alloc(alloc, (usize)u8cap);
        if (buf)
        {
            u32 n = wide_to_utf8(p, wlen, (char*)buf, u8cap);
            if (n > 0)
                out[produced++] = (Mel_Locale_Raw){ .tag = { .data = buf, .len = (size)n } };
            else
                mel_dealloc(alloc, buf);
        }
        p += wlen + 1;
    }

    mel_dealloc(alloc, block);
    return produced;
}

void mel_locale__register_host_providers(void)
{
    static const Mel_Locale_Provider_Desc desc = {
        .name = "win32-mui",
        .enumerate = win32_enumerate,
    };
    mel_locale_provider_register(&desc);
}
