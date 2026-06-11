#include <net/address.h>

#include "net_backend.h"

#include <allocator/allocator.h>
#include <string/str8.h>

#include <stdio.h>
#include <string.h>

Mel_Net_Address mel_net_address_v4(u8 a, u8 b, u8 c, u8 d, u16 port)
{
    Mel_Net_Address r;
    memset(&r, 0, sizeof r);
    r.bytes[0] = a;
    r.bytes[1] = b;
    r.bytes[2] = c;
    r.bytes[3] = d;
    r.port = port;
    return r;
}

Mel_Net_Address mel_net_address_v4_any(u16 port) { return mel_net_address_v4(0, 0, 0, 0, port); }
Mel_Net_Address mel_net_address_v4_loopback(u16 port) { return mel_net_address_v4(127, 0, 0, 1, port); }

Mel_Net_Address mel_net_address_v6_any(u16 port)
{
    Mel_Net_Address r;
    memset(&r, 0, sizeof r);
    r.v6 = true;
    r.port = port;
    return r;
}

Mel_Net_Address mel_net_address_v6_loopback(u16 port)
{
    Mel_Net_Address r = mel_net_address_v6_any(port);
    r.bytes[15] = 1;
    return r;
}

bool mel_net_address_equals(const Mel_Net_Address* a, const Mel_Net_Address* b) { return a->v6 == b->v6 && a->port == b->port && a->scope_id == b->scope_id && memcmp(a->bytes, b->bytes, sizeof a->bytes) == 0; }

bool mel_net_address_is_loopback(const Mel_Net_Address* a)
{
    if (!a->v6)
        return a->bytes[0] == 127;
    for (usize i = 0; i < 15; i++)
        if (a->bytes[i] != 0)
            return false;
    return a->bytes[15] == 1;
}

bool mel_net_address_is_any(const Mel_Net_Address* a)
{
    usize span = a->v6 ? 16 : 4;
    for (usize i = 0; i < span; i++)
        if (a->bytes[i] != 0)
            return false;
    return true;
}

bool mel_net_address_is_v4_mapped(const Mel_Net_Address* a)
{
    if (!a->v6)
        return false;
    for (usize i = 0; i < 10; i++)
        if (a->bytes[i] != 0)
            return false;
    return a->bytes[10] == 0xff && a->bytes[11] == 0xff;
}

static bool parse_v4_into(str8 text, u8 out[4])
{
    usize i = 0;
    for (usize octet = 0; octet < 4; octet++)
    {
        if (octet > 0)
        {
            if (i >= text.len || text.data[i] != '.')
                return false;
            i++;
        }
        usize start = i;
        u32   value = 0;
        while (i < text.len && text.data[i] >= '0' && text.data[i] <= '9')
        {
            value = value * 10 + (u32)(text.data[i] - '0');
            i++;
            if (i - start > 3)
                return false;
        }
        usize digits = i - start;
        if (digits == 0 || value > 255)
            return false;
        if (digits > 1 && text.data[start] == '0')
            return false;
        out[octet] = (u8)value;
    }
    return i == text.len;
}

static bool parse_hex_group(str8 text, u16* out)
{
    if (text.len == 0 || text.len > 4)
        return false;
    u32 value = 0;
    for (usize i = 0; i < text.len; i++)
    {
        u8  c = text.data[i];
        u32 d;
        if (c >= '0' && c <= '9')
            d = (u32)(c - '0');
        else if (c >= 'a' && c <= 'f')
            d = (u32)(c - 'a') + 10;
        else if (c >= 'A' && c <= 'F')
            d = (u32)(c - 'A') + 10;
        else
            return false;
        value = (value << 4) | d;
    }
    *out = (u16)value;
    return true;
}

static bool parse_groups(str8 text, u16* groups, usize max_groups, usize* out_count, bool* out_trailing_v4, u8 v4[4])
{
    *out_count = 0;
    *out_trailing_v4 = false;
    if (text.len == 0)
        return true;

    usize start = 0;
    for (usize i = 0; i <= text.len; i++)
    {
        if (i < text.len && text.data[i] != ':')
            continue;
        str8 part = str8_slice(text, (size)start, (size)(i - start));
        bool last = i == text.len;
        if (part.len == 0)
            return false;
        if (last && str8_contains(part, S8(".")))
        {
            if (!parse_v4_into(part, v4))
                return false;
            if (*out_count + 2 > max_groups)
                return false;
            *out_trailing_v4 = true;
            *out_count += 2;
            return true;
        }
        u16 g = 0;
        if (!parse_hex_group(part, &g))
            return false;
        if (*out_count >= max_groups)
            return false;
        groups[(*out_count)++] = g;
        start = i + 1;
    }
    return true;
}

static Mel_Net_Status parse_v6(str8 text, u16 port, Mel_Net_Address* out)
{
    u32  scope_id = 0;
    size pct = str8_find(text, S8("%"));
    if (pct >= 0)
    {
        str8 zone = str8_suffix(text, (size)text.len - pct - 1);
        text = str8_prefix(text, pct);
        if (zone.len == 0)
            return MEL_NET_ERROR | MEL_NET_BAD_ADDRESS;
        bool numeric = true;
        for (usize i = 0; i < zone.len; i++)
            if (zone.data[i] < '0' || zone.data[i] > '9')
                numeric = false;
        if (numeric)
        {
            for (usize i = 0; i < zone.len; i++)
                scope_id = scope_id * 10 + (u32)(zone.data[i] - '0');
        }
        else
        {
            char name[64];
            if (zone.len >= sizeof name)
                return MEL_NET_ERROR | MEL_NET_BAD_ADDRESS;
            memcpy(name, zone.data, zone.len);
            name[zone.len] = 0;
            scope_id = mel_net__backend_scope_id(name);
            if (scope_id == 0)
                return MEL_NET_ERROR | MEL_NET_BAD_ADDRESS;
        }
    }

    size  gap = str8_find(text, S8("::"));
    u16   left[8], right[8];
    u8    v4l[4] = { 0 }, v4r[4] = { 0 };
    usize nleft = 0, nright = 0;
    bool  v4_left = false, v4_right = false;

    if (gap >= 0)
    {
        str8 ltext = str8_prefix(text, gap);
        str8 rtext = str8_suffix(text, (size)text.len - gap - 2);
        if (str8_contains(rtext, S8("::")))
            return MEL_NET_ERROR | MEL_NET_BAD_ADDRESS;
        if (!parse_groups(ltext, left, 7, &nleft, &v4_left, v4l) || v4_left)
            return MEL_NET_ERROR | MEL_NET_BAD_ADDRESS;
        if (!parse_groups(rtext, right, 7, &nright, &v4_right, v4r))
            return MEL_NET_ERROR | MEL_NET_BAD_ADDRESS;
        if (nleft + nright > 7)
            return MEL_NET_ERROR | MEL_NET_BAD_ADDRESS;
    }
    else
    {
        if (!parse_groups(text, left, 8, &nleft, &v4_left, v4l))
            return MEL_NET_ERROR | MEL_NET_BAD_ADDRESS;
        if (nleft != 8)
            return MEL_NET_ERROR | MEL_NET_BAD_ADDRESS;
    }

    memset(out, 0, sizeof *out);
    out->v6 = true;
    out->port = port;
    out->scope_id = scope_id;

    usize gi = 0;
    usize lcount = v4_left ? nleft - 2 : nleft;
    for (usize i = 0; i < lcount; i++, gi++)
    {
        out->bytes[gi * 2] = (u8)(left[i] >> 8);
        out->bytes[gi * 2 + 1] = (u8)left[i];
    }
    if (v4_left)
    {
        memcpy(&out->bytes[gi * 2], v4l, 4);
        gi += 2;
    }
    if (gap >= 0)
    {
        usize total = 8;
        usize rcount = v4_right ? nright - 2 : nright;
        usize rstart = total - nright;
        gi = rstart;
        for (usize i = 0; i < rcount; i++, gi++)
        {
            out->bytes[gi * 2] = (u8)(right[i] >> 8);
            out->bytes[gi * 2 + 1] = (u8)right[i];
        }
        if (v4_right)
            memcpy(&out->bytes[gi * 2], v4r, 4);
    }
    return MEL_NET_OK;
}

Mel_Net_Status mel_net_address_parse(str8 text, u16 port, Mel_Net_Address* out)
{
    if (!out || text.len == 0)
        return MEL_NET_ERROR | MEL_NET_BAD_ADDRESS;

    if (text.data[0] == '[' && text.data[text.len - 1] == ']')
        text = str8_slice(text, 1, (size)text.len - 2);

    if (str8_contains(text, S8(":")))
        return parse_v6(text, port, out);

    u8 v4[4];
    if (!parse_v4_into(text, v4))
        return MEL_NET_ERROR | MEL_NET_BAD_ADDRESS;
    *out = mel_net_address_v4(v4[0], v4[1], v4[2], v4[3], port);
    return MEL_NET_OK;
}

str8 mel_net_address_format(const Mel_Net_Address* addr, const Mel_Alloc* alloc)
{
    char  buf[80];
    usize len = 0;

    if (!addr->v6)
    {
        len = (usize)snprintf(buf, sizeof buf, "%u.%u.%u.%u", addr->bytes[0], addr->bytes[1], addr->bytes[2], addr->bytes[3]);
    }
    else
    {
        u16 groups[8];
        for (usize i = 0; i < 8; i++)
            groups[i] = (u16)((u16)addr->bytes[i * 2] << 8 | addr->bytes[i * 2 + 1]);

        usize best_start = 0, best_len = 0;
        usize run_start = 0, run_len = 0;
        for (usize i = 0; i < 8; i++)
        {
            if (groups[i] == 0)
            {
                if (run_len == 0)
                    run_start = i;
                run_len++;
                if (run_len > best_len)
                {
                    best_start = run_start;
                    best_len = run_len;
                }
            }
            else
            {
                run_len = 0;
            }
        }
        if (best_len < 2)
            best_len = 0;

        bool mapped = mel_net_address_is_v4_mapped(addr);
        bool need_sep = false;
        for (usize i = 0; i < 8;)
        {
            if (best_len > 0 && i == best_start)
            {
                buf[len++] = ':';
                buf[len++] = ':';
                need_sep = false;
                i += best_len;
                continue;
            }
            if (need_sep)
                buf[len++] = ':';
            if (mapped && i == 6)
            {
                len += (usize)snprintf(buf + len, sizeof buf - len, "%u.%u.%u.%u", addr->bytes[12], addr->bytes[13], addr->bytes[14], addr->bytes[15]);
                i = 8;
                break;
            }
            len += (usize)snprintf(buf + len, sizeof buf - len, "%x", groups[i]);
            need_sep = true;
            i++;
        }
        if (addr->scope_id != 0)
            len += (usize)snprintf(buf + len, sizeof buf - len, "%%%u", addr->scope_id);
    }

    str8 tmp = { (u8*)buf, (size)len };
    return str8_dup_alloc(tmp, alloc);
}
