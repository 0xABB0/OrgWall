#include "wire.h"

#include <allocator/allocator.h>

#include <string.h>

bool mel_http__header_ieq(str8 name, const char* want) { return str8_ieq_cstr(name, want); }

void mel_http__parser_init(Mel_Http__Resp_Parser* p, const Mel_Alloc* alloc, usize max_head, bool head_request)
{
    memset(p, 0, sizeof *p);
    p->alloc = alloc;
    p->max_head = max_head;
    p->head_request = head_request;
    p->content_length = -1;
    mel_array_init(&p->head, alloc);
    mel_array_init(&p->headers, alloc);
    mel_array_init(&p->chunk_line, alloc);
}

void mel_http__parser_free(Mel_Http__Resp_Parser* p)
{
    mel_array_free(&p->head);
    mel_array_free(&p->headers);
    mel_array_free(&p->chunk_line);
}

static str8 line_strip_cr(str8 line)
{
    if (line.len > 0 && line.data[line.len - 1] == '\r')
        line.len--;
    return line;
}

static Mel_Http_Status parse_status_line(Mel_Http__Resp_Parser* p, str8 line)
{
    if (str8_starts_with(line, S8("HTTP/1.1 ")))
        p->http10 = false;
    else if (str8_starts_with(line, S8("HTTP/1.0 ")))
        p->http10 = true;
    else
        return MEL_HTTP_ERROR | MEL_HTTP_MALFORMED;

    str8 rest = str8_suffix(line, (size)line.len - 9);
    if (rest.len < 3)
        return MEL_HTTP_ERROR | MEL_HTTP_MALFORMED;
    i32 code = 0;
    for (usize i = 0; i < 3; i++)
    {
        u8 c = rest.data[i];
        if (c < '0' || c > '9')
            return MEL_HTTP_ERROR | MEL_HTTP_MALFORMED;
        code = code * 10 + (c - '0');
    }
    p->status_code = code;
    p->reason = STR8_EMPTY;
    if (rest.len > 3)
    {
        if (rest.data[3] != ' ')
            return MEL_HTTP_ERROR | MEL_HTTP_MALFORMED;
        p->reason = str8_suffix(rest, (size)rest.len - 4);
    }
    return MEL_HTTP_OK;
}

static Mel_Http_Status parse_header_line(Mel_Http__Resp_Parser* p, str8 line)
{
    if (line.len > 0 && (line.data[0] == ' ' || line.data[0] == '\t'))
        return MEL_HTTP_ERROR | MEL_HTTP_MALFORMED;

    size colon = str8_find(line, S8(":"));
    if (colon <= 0)
        return MEL_HTTP_ERROR | MEL_HTTP_MALFORMED;

    Mel_Http_Header h;
    h.name = str8_prefix(line, colon);
    h.value = str8_trim(str8_suffix(line, (size)line.len - colon - 1));

    if (mel_http__header_ieq(h.name, "content-length"))
    {
        if (h.value.len == 0)
            return MEL_HTTP_ERROR | MEL_HTTP_MALFORMED;
        i64 v = 0;
        for (usize i = 0; i < h.value.len; i++)
        {
            u8 c = h.value.data[i];
            if (c < '0' || c > '9')
                return MEL_HTTP_ERROR | MEL_HTTP_MALFORMED;
            v = v * 10 + (c - '0');
            if (v > (i64)1 << 53)
                return MEL_HTTP_ERROR | MEL_HTTP_MALFORMED;
        }
        if (p->content_length >= 0 && p->content_length != v)
            return MEL_HTTP_ERROR | MEL_HTTP_MALFORMED;
        p->content_length = v;
    }
    else if (mel_http__header_ieq(h.name, "transfer-encoding"))
    {
        if (str8_contains(h.value, S8("chunked")) || str8_contains(h.value, S8("Chunked")))
            p->chunked = true;
    }
    else if (mel_http__header_ieq(h.name, "connection"))
    {
        if (str8_ieq_cstr(str8_trim(h.value), "close"))
            p->connection_close = true;
        else if (str8_ieq_cstr(str8_trim(h.value), "keep-alive"))
            p->keep_alive_header = true;
    }

    mel_array_push(&p->headers, h);
    return MEL_HTTP_OK;
}

static void parser_reset_interim(Mel_Http__Resp_Parser* p)
{
    mel_array_clear(&p->head);
    mel_array_clear(&p->headers);
    p->scanned = 0;
    p->status_code = 0;
    p->reason = STR8_EMPTY;
    p->content_length = -1;
    p->chunked = false;
    p->connection_close = false;
    p->keep_alive_header = false;
}

static Mel_Http_Status parse_head(Mel_Http__Resp_Parser* p)
{
    str8  all = { p->head.items, (size)p->head.count };
    usize start = 0;
    bool  first = true;

    for (usize i = 0; i < all.len; i++)
    {
        if (all.data[i] != '\n')
            continue;
        str8 line = line_strip_cr(str8_slice(all, (size)start, (size)(i - start)));
        start = i + 1;

        if (first)
        {
            Mel_Http_Status st = parse_status_line(p, line);
            if (!mel_http_status_ok(st))
                return st;
            first = false;
            continue;
        }
        if (line.len == 0)
            break;
        Mel_Http_Status st = parse_header_line(p, line);
        if (!mel_http_status_ok(st))
            return st;
    }

    if (p->status_code >= 100 && p->status_code <= 199)
    {
        parser_reset_interim(p);
        return MEL_HTTP_OK;
    }

    p->head_done = true;

    bool no_body = p->head_request || p->status_code == 204 || p->status_code == 304;
    if (no_body)
    {
        p->body_mode = MEL_HTTP__BODY_NONE;
        p->done = true;
    }
    else if (p->chunked)
    {
        p->body_mode = MEL_HTTP__BODY_CHUNKED;
        p->chunk_phase = MEL_HTTP__CHUNK_SIZE;
    }
    else if (p->content_length >= 0)
    {
        p->body_mode = MEL_HTTP__BODY_LENGTH;
        p->remaining = (u64)p->content_length;
        if (p->remaining == 0)
            p->done = true;
    }
    else
    {
        p->body_mode = MEL_HTTP__BODY_EOF;
        p->connection_close = true;
    }
    return MEL_HTTP_OK;
}

static Mel_Http_Status feed_head(Mel_Http__Resp_Parser* p, const u8* data, usize len, usize* out_consumed)
{
    usize take = len;
    if (p->head.count + take > p->max_head)
        take = p->max_head - p->head.count;

    for (usize i = 0; i < take; i++)
        mel_array_push(&p->head, data[i]);

    usize end = 0;
    bool  found = false;
    while (p->scanned < p->head.count)
    {
        usize i = p->scanned;
        if (p->head.items[i] == '\n')
        {
            bool blank_crlf = i >= 3 && p->head.items[i - 1] == '\r' && p->head.items[i - 2] == '\n' && p->head.items[i - 3] == '\r';
            bool blank_lf = i >= 1 && p->head.items[i - 1] == '\n';
            if (blank_crlf || blank_lf)
            {
                end = i + 1;
                found = true;
                p->scanned = i + 1;
                break;
            }
        }
        p->scanned++;
    }

    if (!found)
    {
        if (p->head.count >= p->max_head)
            return MEL_HTTP_ERROR | MEL_HTTP_MALFORMED;
        *out_consumed = take;
        return MEL_HTTP_OK;
    }

    usize overshoot = p->head.count - end;
    *out_consumed = take - overshoot;
    p->head.count = end;

    return parse_head(p);
}

static Mel_Http_Status feed_chunked(Mel_Http__Resp_Parser* p, const u8* data, usize len, usize* out_consumed, Mel_Http__Body_Fn on_body, void* user)
{
    usize i = 0;
    while (i < len && !p->done)
    {
        if (p->chunk_phase == MEL_HTTP__CHUNK_SIZE || p->chunk_phase == MEL_HTTP__CHUNK_TRAILER)
        {
            u8 c = data[i++];
            if (c == '\n')
            {
                str8 line = line_strip_cr((str8){ p->chunk_line.items, (size)p->chunk_line.count });
                if (p->chunk_phase == MEL_HTTP__CHUNK_SIZE)
                {
                    size semi = str8_find(line, S8(";"));
                    str8 hex = semi >= 0 ? str8_prefix(line, semi) : line;
                    hex = str8_trim(hex);
                    if (hex.len == 0 || hex.len > 16)
                        return MEL_HTTP_ERROR | MEL_HTTP_MALFORMED;
                    u64 v = 0;
                    for (usize k = 0; k < hex.len; k++)
                    {
                        u8  h = hex.data[k];
                        u64 d;
                        if (h >= '0' && h <= '9')
                            d = (u64)(h - '0');
                        else if (h >= 'a' && h <= 'f')
                            d = (u64)(h - 'a') + 10;
                        else if (h >= 'A' && h <= 'F')
                            d = (u64)(h - 'A') + 10;
                        else
                            return MEL_HTTP_ERROR | MEL_HTTP_MALFORMED;
                        v = (v << 4) | d;
                    }
                    if (v == 0)
                        p->chunk_phase = MEL_HTTP__CHUNK_TRAILER;
                    else
                    {
                        p->chunk_remaining = v;
                        p->chunk_phase = MEL_HTTP__CHUNK_DATA;
                    }
                }
                else
                {
                    if (line.len == 0)
                        p->done = true;
                }
                mel_array_clear(&p->chunk_line);
            }
            else
            {
                if (p->chunk_line.count > 1024)
                    return MEL_HTTP_ERROR | MEL_HTTP_MALFORMED;
                mel_array_push(&p->chunk_line, c);
            }
        }
        else if (p->chunk_phase == MEL_HTTP__CHUNK_DATA)
        {
            usize span = len - i;
            if ((u64)span > p->chunk_remaining)
                span = (usize)p->chunk_remaining;
            if (on_body && span > 0)
                on_body(user, (str8){ (u8*)data + i, (size)span });
            i += span;
            p->chunk_remaining -= span;
            if (p->chunk_remaining == 0)
                p->chunk_phase = MEL_HTTP__CHUNK_DATA_CRLF;
        }
        else
        {
            u8 c = data[i++];
            if (c == '\n')
                p->chunk_phase = MEL_HTTP__CHUNK_SIZE;
            else if (c != '\r')
                return MEL_HTTP_ERROR | MEL_HTTP_MALFORMED;
        }
    }
    *out_consumed = i;
    return MEL_HTTP_OK;
}

Mel_Http_Status mel_http__parser_feed(Mel_Http__Resp_Parser* p, const u8* data, usize len, usize* out_consumed, Mel_Http__Body_Fn on_body, void* user)
{
    *out_consumed = 0;
    usize i = 0;

    while (i < len && !p->done)
    {
        if (!p->head_done)
        {
            usize           used = 0;
            Mel_Http_Status st = feed_head(p, data + i, len - i, &used);
            i += used;
            if (!mel_http_status_ok(st))
            {
                *out_consumed = i;
                return st;
            }
            continue;
        }

        if (p->body_mode == MEL_HTTP__BODY_LENGTH)
        {
            usize span = len - i;
            if ((u64)span > p->remaining)
                span = (usize)p->remaining;
            if (on_body && span > 0)
                on_body(user, (str8){ (u8*)data + i, (size)span });
            i += span;
            p->remaining -= span;
            if (p->remaining == 0)
                p->done = true;
        }
        else if (p->body_mode == MEL_HTTP__BODY_CHUNKED)
        {
            usize           used = 0;
            Mel_Http_Status st = feed_chunked(p, data + i, len - i, &used, on_body, user);
            i += used;
            if (!mel_http_status_ok(st))
            {
                *out_consumed = i;
                return st;
            }
        }
        else if (p->body_mode == MEL_HTTP__BODY_EOF)
        {
            if (on_body && len - i > 0)
                on_body(user, (str8){ (u8*)data + i, (size)(len - i) });
            i = len;
        }
        else
        {
            break;
        }
    }

    *out_consumed = i;
    return MEL_HTTP_OK;
}

Mel_Http_Status mel_http__parser_finish_eof(Mel_Http__Resp_Parser* p)
{
    if (p->done)
        return MEL_HTTP_OK;
    if (p->head_done && p->body_mode == MEL_HTTP__BODY_EOF)
    {
        p->done = true;
        return MEL_HTTP_OK;
    }
    return MEL_HTTP_ERROR | MEL_HTTP_BODY_INCOMPLETE;
}

bool mel_http__parser_keep_alive(const Mel_Http__Resp_Parser* p)
{
    if (!p->done || p->connection_close)
        return false;
    if (p->http10)
        return p->keep_alive_header;
    return true;
}

static void buf_append(Mel_Http__Buf* buf, str8 s)
{
    for (usize i = 0; i < s.len; i++)
        mel_array_push(buf, s.data[i]);
}

str8 mel_http__wire_request_head(const Mel_Alloc* alloc, str8 method, str8 target, str8 host, u16 port, bool default_port, const Mel_Http_Header* headers, usize header_count, i64 body_len)
{
    bool has_host = false, has_length = false, has_te = false;
    for (usize i = 0; i < header_count; i++)
    {
        if (mel_http__header_ieq(headers[i].name, "host"))
            has_host = true;
        else if (mel_http__header_ieq(headers[i].name, "content-length"))
            has_length = true;
        else if (mel_http__header_ieq(headers[i].name, "transfer-encoding"))
            has_te = true;
    }

    Mel_Http__Buf buf;
    mel_array_init(&buf, alloc);

    buf_append(&buf, method);
    buf_append(&buf, S8(" "));
    buf_append(&buf, target);
    buf_append(&buf, S8(" HTTP/1.1\r\n"));

    if (!has_host)
    {
        buf_append(&buf, S8("Host: "));
        buf_append(&buf, host);
        if (!default_port)
        {
            char  pb[8];
            usize n = 0;
            u16   v = port;
            char  tmp[8];
            usize t = 0;
            if (v == 0)
                tmp[t++] = '0';
            while (v > 0)
            {
                tmp[t++] = (char)('0' + v % 10);
                v /= 10;
            }
            pb[n++] = ':';
            while (t > 0)
                pb[n++] = tmp[--t];
            buf_append(&buf, (str8){ (u8*)pb, (size)n });
        }
        buf_append(&buf, S8("\r\n"));
    }

    for (usize i = 0; i < header_count; i++)
    {
        buf_append(&buf, headers[i].name);
        buf_append(&buf, S8(": "));
        buf_append(&buf, headers[i].value);
        buf_append(&buf, S8("\r\n"));
    }

    if (!has_length && !has_te && body_len > 0)
    {
        char  lb[32];
        usize n = 0;
        i64   v = body_len;
        char  tmp[24];
        usize t = 0;
        while (v > 0)
        {
            tmp[t++] = (char)('0' + v % 10);
            v /= 10;
        }
        memcpy(lb, "Content-Length: ", 16);
        n = 16;
        while (t > 0)
            lb[n++] = tmp[--t];
        buf_append(&buf, (str8){ (u8*)lb, (size)n });
        buf_append(&buf, S8("\r\n"));
    }

    buf_append(&buf, S8("\r\n"));

    str8 out = { buf.items, (size)buf.count };
    return out;
}
