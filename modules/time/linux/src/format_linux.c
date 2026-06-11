#include <time/format_provider.h>

#include <langinfo.h>
#include <locale.h>
#include <string.h>

static u32 order_from_d_fmt(const char* fmt)
{
    int y = -1, m = -1, d = -1, idx = 0;
    for (const char* p = fmt; *p; p++)
    {
        if (*p != '%')
            continue;
        p++;
        if (*p == 'E' || *p == 'O')
            p++;
        switch (*p)
        {
            case 'Y':
            case 'y':
                if (y < 0)
                    y = idx++;
                break;
            case 'm':
                if (m < 0)
                    m = idx++;
                break;
            case 'd':
            case 'e':
                if (d < 0)
                    d = idx++;
                break;
            default:
                break;
        }
        if (!*p)
            break;
    }
    if (y < 0 || m < 0 || d < 0)
        return 0;
    if (y < m && y < d)
        return MEL_DATE_ORDER_YMD;
    if (d < m)
        return MEL_DATE_ORDER_DMY;
    return MEL_DATE_ORDER_MDY;
}

static char separator_from_d_fmt(const char* fmt)
{
    bool seen_field = false;
    for (const char* p = fmt; *p; p++)
    {
        if (*p == '%')
        {
            p++;
            if (*p == 'E' || *p == 'O')
                p++;
            if (!*p)
                break;
            seen_field = true;
            continue;
        }
        if (seen_field && *p != ' ')
            return *p;
    }
    return '/';
}

static bool linux_query(void* user, Mel_Time_Format_Prefs* out)
{
    (void)user;
    setlocale(LC_TIME, "");

    const char* d_fmt = nl_langinfo(D_FMT);
    if (!d_fmt || !d_fmt[0])
        return false;
    u32 order = order_from_d_fmt(d_fmt);
    if (order == 0)
        return false;

    const char* t_fmt = nl_langinfo(T_FMT);
    bool        is12 = false;
    if (t_fmt)
        for (const char* p = t_fmt; *p; p++)
            if (*p == '%' && (p[1] == 'I' || p[1] == 'p' || p[1] == 'r'))
            {
                is12 = true;
                break;
            }

    memset(out, 0, sizeof *out);
    out->date_order = order;
    out->clock = is12 ? MEL_CLOCK_12H : MEL_CLOCK_24H;
    out->date_separator[0] = separator_from_d_fmt(d_fmt);
    out->date_separator[1] = '\0';
    return true;
}

void mel_time_format__register_host_providers(void)
{
    static const Mel_Time_Format_Provider_Desc desc = {
        .name = "linux-langinfo",
        .query = linux_query,
    };
    mel_time_format_provider_register(&desc);
}
