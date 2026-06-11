#include <http/url.h>

#include <allocator/allocator.h>
#include <string/str8.h>

#include <string.h>

static bool scheme_default_port(str8 scheme, u16* out)
{
    if (str8_ieq_cstr(scheme, "http"))
    {
        *out = 80;
        return true;
    }
    if (str8_ieq_cstr(scheme, "https"))
    {
        *out = 443;
        return true;
    }
    if (str8_ieq_cstr(scheme, "ws"))
    {
        *out = 80;
        return true;
    }
    if (str8_ieq_cstr(scheme, "wss"))
    {
        *out = 443;
        return true;
    }
    return false;
}

u16 mel_http_url_effective_port(const Mel_Http_Url* url)
{
    if (url->port_explicit)
        return url->port;
    u16 p = 0;
    if (scheme_default_port(url->scheme, &p))
        return p;
    return 0;
}

Mel_Http_Status mel_http_url_parse(str8 text, Mel_Http_Url* out)
{
    if (!out || text.len == 0)
        return MEL_HTTP_ERROR | MEL_HTTP_BAD_URL;
    memset(out, 0, sizeof *out);

    size sep = str8_find(text, S8("://"));
    if (sep <= 0)
        return MEL_HTTP_ERROR | MEL_HTTP_BAD_URL;
    out->scheme = str8_prefix(text, sep);
    for (usize i = 0; i < out->scheme.len; i++)
    {
        u8   c = out->scheme.data[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
        if (!ok || (i == 0 && !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))))
            return MEL_HTTP_ERROR | MEL_HTTP_BAD_URL;
    }

    str8 rest = str8_suffix(text, (size)text.len - sep - 3);

    usize auth_end = rest.len;
    for (usize i = 0; i < rest.len; i++)
    {
        u8 c = rest.data[i];
        if (c == '/' || c == '?' || c == '#')
        {
            auth_end = i;
            break;
        }
    }
    str8 authority = str8_prefix(rest, (size)auth_end);
    str8 tail = str8_suffix(rest, (size)(rest.len - auth_end));

    size at = str8_rfind(authority, S8("@"));
    if (at >= 0)
    {
        out->userinfo = str8_prefix(authority, at);
        authority = str8_suffix(authority, (size)authority.len - at - 1);
    }

    if (authority.len == 0)
        return MEL_HTTP_ERROR | MEL_HTTP_BAD_URL;

    str8 port_text = STR8_EMPTY;
    if (authority.data[0] == '[')
    {
        size close = str8_find(authority, S8("]"));
        if (close < 0)
            return MEL_HTTP_ERROR | MEL_HTTP_BAD_URL;
        out->host = str8_slice(authority, 0, close + 1);
        str8 after = str8_suffix(authority, (size)authority.len - close - 1);
        if (after.len > 0)
        {
            if (after.data[0] != ':')
                return MEL_HTTP_ERROR | MEL_HTTP_BAD_URL;
            port_text = str8_suffix(after, (size)after.len - 1);
        }
    }
    else
    {
        size colon = str8_rfind(authority, S8(":"));
        if (colon >= 0)
        {
            out->host = str8_prefix(authority, colon);
            port_text = str8_suffix(authority, (size)authority.len - colon - 1);
        }
        else
        {
            out->host = authority;
        }
    }

    if (out->host.len == 0)
        return MEL_HTTP_ERROR | MEL_HTTP_BAD_URL;

    if (port_text.len > 0)
    {
        u32 port = 0;
        for (usize i = 0; i < port_text.len; i++)
        {
            u8 c = port_text.data[i];
            if (c < '0' || c > '9')
                return MEL_HTTP_ERROR | MEL_HTTP_BAD_URL;
            port = port * 10 + (u32)(c - '0');
            if (port > 65535)
                return MEL_HTTP_ERROR | MEL_HTTP_BAD_URL;
        }
        out->port = (u16)port;
        out->port_explicit = true;
    }

    usize path_end = tail.len;
    for (usize i = 0; i < tail.len; i++)
    {
        if (tail.data[i] == '?' || tail.data[i] == '#')
        {
            path_end = i;
            break;
        }
    }
    out->path = str8_prefix(tail, (size)path_end);
    str8 after_path = str8_suffix(tail, (size)(tail.len - path_end));

    if (after_path.len > 0 && after_path.data[0] == '?')
    {
        after_path = str8_suffix(after_path, (size)after_path.len - 1);
        usize q_end = after_path.len;
        for (usize i = 0; i < after_path.len; i++)
        {
            if (after_path.data[i] == '#')
            {
                q_end = i;
                break;
            }
        }
        out->query = str8_prefix(after_path, (size)q_end);
        after_path = str8_suffix(after_path, (size)(after_path.len - q_end));
    }
    if (after_path.len > 0 && after_path.data[0] == '#')
        out->fragment = str8_suffix(after_path, (size)after_path.len - 1);

    return MEL_HTTP_OK;
}

str8 mel_http_url_target(const Mel_Http_Url* url, const Mel_Alloc* alloc)
{
    str8 path = url->path.len > 0 ? url->path : S8("/");
    if (url->query.len == 0)
        return str8_dup_alloc(path, alloc);
    return str8_fmt_alloc(alloc, "%.*s?%.*s", (int)path.len, path.data, (int)url->query.len, url->query.data);
}

static bool percent_unreserved(u8 c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~'; }

str8 mel_http_percent_encode(str8 text, const Mel_Alloc* alloc)
{
    static const char HEX[] = "0123456789ABCDEF";
    usize             out_len = 0;
    for (usize i = 0; i < text.len; i++)
        out_len += percent_unreserved(text.data[i]) ? 1 : 3;

    u8* data = mel_alloc(alloc, out_len);
    if (!data)
        return STR8_EMPTY;

    usize w = 0;
    for (usize i = 0; i < text.len; i++)
    {
        u8 c = text.data[i];
        if (percent_unreserved(c))
        {
            data[w++] = c;
        }
        else
        {
            data[w++] = '%';
            data[w++] = (u8)HEX[c >> 4];
            data[w++] = (u8)HEX[c & 0xf];
        }
    }
    return (str8){ data, (size)out_len };
}

static i32 hex_digit(u8 c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

Mel_Http_Status mel_http_percent_decode(str8 text, const Mel_Alloc* alloc, str8* out)
{
    u8* data = mel_alloc(alloc, text.len > 0 ? text.len : 1);
    if (!data)
        return MEL_HTTP_ERROR;

    usize w = 0;
    for (usize i = 0; i < text.len;)
    {
        u8 c = text.data[i];
        if (c == '%')
        {
            if (i + 2 >= text.len)
            {
                mel_dealloc(alloc, data);
                return MEL_HTTP_ERROR | MEL_HTTP_BAD_URL;
            }
            i32 hi = hex_digit(text.data[i + 1]);
            i32 lo = hex_digit(text.data[i + 2]);
            if (hi < 0 || lo < 0)
            {
                mel_dealloc(alloc, data);
                return MEL_HTTP_ERROR | MEL_HTTP_BAD_URL;
            }
            data[w++] = (u8)((hi << 4) | lo);
            i += 3;
        }
        else
        {
            data[w++] = c;
            i++;
        }
    }
    *out = (str8){ data, (size)w };
    return MEL_HTTP_OK;
}
