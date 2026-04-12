#include "string.path.h"
#include "string.str8.h"

str8 mel_path_normalize(str8 path, u8* buf, usize buf_cap)
{
    if (path.len == 0) return (str8){ .data = buf, .len = 0 };

    usize w = 0;
    usize r = 0;

    if (buf_cap > 0) buf[w++] = '/';

    while (r < (usize)path.len) {
        u8 c = path.data[r];

        if (c == '\\') c = '/';

        if (c == '/') {
            if (w > 0 && buf[w - 1] == '/') { r++; continue; }
            if (w < buf_cap) buf[w++] = '/';
            r++;
            continue;
        }

        if (c == '.') {
            usize ahead = r + 1;
            u8 next = (ahead < (usize)path.len) ? path.data[ahead] : 0;
            if (next == '\\') next = '/';

            if (next == '/' || next == 0) {
                r = (next == 0) ? ahead : ahead + 1;
                continue;
            }

            if (next == '.') {
                usize ahead2 = ahead + 1;
                u8 next2 = (ahead2 < (usize)path.len) ? path.data[ahead2] : 0;
                if (next2 == '\\') next2 = '/';

                if (next2 == '/' || next2 == 0) {
                    if (w > 1) {
                        w--;
                        while (w > 1 && buf[w - 1] != '/') w--;
                    }
                    r = (next2 == 0) ? ahead2 : ahead2 + 1;
                    continue;
                }
            }
        }

        while (r < (usize)path.len) {
            u8 ch = path.data[r];
            if (ch == '/' || ch == '\\') break;
            if (w < buf_cap) buf[w++] = ch;
            r++;
        }
    }

    if (w > 1 && buf[w - 1] == '/') w--;

    return (str8){ .data = buf, .len = (size)w };
}

str8 mel_path_join(str8 base, str8 relative, u8* buf, usize buf_cap)
{
    if (base.len == 0) return mel_path_normalize(relative, buf, buf_cap);
    if (relative.len == 0) return mel_path_normalize(base, buf, buf_cap);

    if (relative.len > 0 && (relative.data[0] == '/' || relative.data[0] == '\\'))
        return mel_path_normalize(relative, buf, buf_cap);

    u8 temp[8192];
    usize t = 0;

    for (usize i = 0; i < (usize)base.len && t < sizeof(temp); i++)
        temp[t++] = base.data[i];

    if (t > 0 && t < sizeof(temp) && temp[t - 1] != '/' && temp[t - 1] != '\\')
        temp[t++] = '/';

    for (usize i = 0; i < (usize)relative.len && t < sizeof(temp); i++)
        temp[t++] = relative.data[i];

    return mel_path_normalize((str8){ .data = temp, .len = (size)t }, buf, buf_cap);
}

str8 mel_path_parent(str8 path)
{
    if (path.len <= 1) return path;

    size end = path.len;
    if (path.data[end - 1] == '/' || path.data[end - 1] == '\\') end--;

    while (end > 0 && path.data[end - 1] != '/' && path.data[end - 1] != '\\')
        end--;

    if (end <= 1) return (str8){ .data = path.data, .len = 1 };

    return (str8){ .data = path.data, .len = end - 1 };
}

str8 mel_path_filename(str8 path)
{
    if (path.len == 0) return path;

    size i = path.len;
    while (i > 0 && path.data[i - 1] != '/' && path.data[i - 1] != '\\')
        i--;

    return (str8){ .data = path.data + i, .len = path.len - i };
}

str8 mel_path_extension(str8 path)
{
    str8 name = mel_path_filename(path);
    if (name.len == 0) return (str8){0};

    size i = name.len;
    while (i > 0 && name.data[i - 1] != '.')
        i--;

    if (i == 0) return (str8){0};

    return (str8){ .data = name.data + i, .len = name.len - i };
}

bool mel_path_is_absolute(str8 path)
{
    return path.len > 0 && (path.data[0] == '/' || path.data[0] == '\\');
}
