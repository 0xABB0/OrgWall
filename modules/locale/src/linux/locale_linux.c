#include <locale/provider.h>
#include <log/log.h>

#include <allocator/allocator.h>

#include <stdlib.h>
#include <string.h>

static size token_len(const char* s)
{
    size n = 0;
    while (s[n] && s[n] != '.' && s[n] != '@' && s[n] != ':')
        n++;
    return n;
}

static bool emit(const Mel_Alloc* alloc, Mel_Locale_Raw* out, u32 cap, u32* produced, const char* begin, size len)
{
    if (len == 0)
        return true;
    if (len == 1 && (begin[0] == 'C' || begin[0] == 'c'))
        return true;
    if (len == 5 && memcmp(begin, "POSIX", 5) == 0)
        return true;
    if (*produced >= cap)
        return false;
    u8* buf = (u8*)mel_alloc(alloc, (usize)len);
    if (!buf)
        return true;
    memcpy(buf, begin, (usize)len);
    out[(*produced)++] = (Mel_Locale_Raw){ .tag = { .data = buf, .len = len } };
    return true;
}

static u32 count_language_list(const char* v)
{
    u32         n = 0;
    const char* p = v;
    while (*p)
    {
        size len = token_len(p);
        if (!(len == 0 || (len == 1 && (p[0] == 'C' || p[0] == 'c')) || (len == 5 && memcmp(p, "POSIX", 5) == 0)))
            n++;
        p += len;
        while (*p && *p != ':')
            p++;
        if (*p == ':')
            p++;
    }
    return n;
}

static u32 linux_enumerate(void* user, const Mel_Alloc* alloc, Mel_Locale_Raw* out, u32 cap)
{
    (void)user;

    const char* language = getenv("LANGUAGE");
    if (language && language[0])
    {
        u32 need = count_language_list(language);
        if (need > cap)
            return need;
        u32         produced = 0;
        const char* p = language;
        while (*p)
        {
            size len = token_len(p);
            if (!emit(alloc, out, cap, &produced, p, len))
                break;
            p += len;
            while (*p && *p != ':')
                p++;
            if (*p == ':')
                p++;
        }
        if (produced > 0)
            return produced;
    }

    const char* single = getenv("LC_ALL");
    if (!single || !single[0])
        single = getenv("LC_MESSAGES");
    if (!single || !single[0])
        single = getenv("LANG");
    if (!single || !single[0])
        return 0;

    size len = token_len(single);
    if (len == 0)
        return 0;
    if (cap < 1)
        return 1;
    u32 produced = 0;
    emit(alloc, out, cap, &produced, single, len);
    return produced;
}

void mel_locale__register_host_providers(void)
{
    static const Mel_Locale_Provider_Desc desc = {
        .name = "linux-env",
        .enumerate = linux_enumerate,
    };
    mel_locale_provider_register(&desc);
}
