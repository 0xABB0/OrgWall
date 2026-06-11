#include <time/format_provider.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string.h>

static u32 order_from_short_date(const WCHAR* fmt)
{
    int y = -1, m = -1, d = -1, idx = 0;
    bool prev_y = false, prev_m = false, prev_d = false;
    for (const WCHAR* p = fmt; *p; p++)
    {
        WCHAR c = *p;
        if ((c == L'y' || c == L'Y') && !prev_y)
        {
            if (y < 0)
                y = idx++;
        }
        else if (c == L'M' && !prev_m)
        {
            if (m < 0)
                m = idx++;
        }
        else if ((c == L'd' || c == L'D') && !prev_d)
        {
            if (d < 0)
                d = idx++;
        }
        prev_y = (c == L'y' || c == L'Y');
        prev_m = (c == L'M');
        prev_d = (c == L'd' || c == L'D');
    }
    if (y < 0 || m < 0 || d < 0)
        return 0;
    if (y < m && y < d)
        return MEL_DATE_ORDER_YMD;
    if (d < m)
        return MEL_DATE_ORDER_DMY;
    return MEL_DATE_ORDER_MDY;
}

static char separator_from_short_date(const WCHAR* fmt)
{
    for (const WCHAR* p = fmt; *p; p++)
    {
        WCHAR c = *p;
        if (c == L'y' || c == L'Y' || c == L'M' || c == L'd' || c == L'D' || c == L' ')
            continue;
        if (c < 128)
            return (char)c;
    }
    return '/';
}

static bool win32_query(void* user, Mel_Time_Format_Prefs* out)
{
    (void)user;

    WCHAR shortdate[80];
    if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SSHORTDATE, shortdate, 80) == 0)
        return false;
    u32 order = order_from_short_date(shortdate);
    if (order == 0)
        return false;

    WCHAR itime[8];
    bool  is12 = false;
    if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_ITIME, itime, 8) != 0)
        is12 = itime[0] == L'0';

    memset(out, 0, sizeof *out);
    out->date_order = order;
    out->clock = is12 ? MEL_CLOCK_12H : MEL_CLOCK_24H;
    out->date_separator[0] = separator_from_short_date(shortdate);
    out->date_separator[1] = '\0';
    return true;
}

void mel_time_format__register_host_providers(void)
{
    static const Mel_Time_Format_Provider_Desc desc = {
        .name = "win32-getlocaleinfoex",
        .query = win32_query,
    };
    mel_time_format_provider_register(&desc);
}
