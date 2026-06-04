#include <fs/dir.h>

#include <core/types.h>

static u8 lower(u8 c, bool ci)
{
    if (ci && c >= 'A' && c <= 'Z')
        return (u8)(c + 32);
    return c;
}

bool mel_fs_glob_match(str8 pattern, str8 name, bool case_insensitive)
{
    size pi = 0, ni = 0;
    size star_p = -1, star_n = 0;

    while (ni < name.len)
    {
        if (pi < pattern.len && (pattern.data[pi] == '?' || lower(pattern.data[pi], case_insensitive) == lower(name.data[ni], case_insensitive)))
        {
            pi++;
            ni++;
        }
        else if (pi < pattern.len && pattern.data[pi] == '*')
        {
            star_p = pi;
            star_n = ni;
            pi++;
        }
        else if (star_p >= 0)
        {
            pi = star_p + 1;
            star_n++;
            ni = star_n;
        }
        else
        {
            return false;
        }
    }
    while (pi < pattern.len && pattern.data[pi] == '*')
        pi++;
    return pi == pattern.len;
}
