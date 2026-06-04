#include <guid/guid.h>

#include <hash/xxh.h>
#include <string/str8.h>

#include <string.h>

static void put_le16(u8* p, u16 v)
{
    p[0] = (u8)(v & 0xFFu);
    p[1] = (u8)((v >> 8) & 0xFFu);
}

static u16 get_le16(const u8* p) { return (u16)((u16)p[0] | ((u16)p[1] << 8)); }

u64 mel_guid_hash(Mel_Guid g) { return mel_xxh3_64(g.bytes, sizeof g.bytes); }

Mel_Guid mel_guid_from_bytes(const u8 bytes[16])
{
    Mel_Guid g;
    memcpy(g.bytes, bytes, 16);
    return g;
}

Mel_Guid mel_guid_from_hidapi(u16 bus, u16 vendor, u16 product, u16 version, const char* name, u8 driver_signature, u8 driver_data)
{
    Mel_Guid g = MEL_GUID_ZERO;
    put_le16(&g.bytes[0], bus);
    if (vendor != 0 || product != 0)
    {
        put_le16(&g.bytes[4], vendor);
        put_le16(&g.bytes[8], product);
        put_le16(&g.bytes[12], version);
        g.bytes[14] = driver_signature;
        g.bytes[15] = driver_data;
    }
    else if (name != NULL)
    {
        usize n = strlen(name);
        if (n > 11)
            n = 11;
        g.bytes[14] = driver_signature;
        g.bytes[15] = driver_data;
        memcpy(&g.bytes[4], name, n);
    }
    return g;
}

Mel_Guid mel_guid_from_vidpid(u16 vendor, u16 product, u16 version) { return mel_guid_from_hidapi(3, vendor, product, version, NULL, 0, 0); }

static char hex_digit(u8 v) { return (char)(v < 10 ? '0' + v : 'a' + (v - 10)); }

static bool hex_value(char c, u8* out)
{
    if (c >= '0' && c <= '9')
        *out = (u8)(c - '0');
    else if (c >= 'a' && c <= 'f')
        *out = (u8)(10 + (c - 'a'));
    else if (c >= 'A' && c <= 'F')
        *out = (u8)(10 + (c - 'A'));
    else
        return false;
    return true;
}

size mel_guid_to_string(Mel_Guid g, char* out, size cap)
{
    if (cap < 33)
        return 0;
    for (u32 i = 0; i < 16; i++)
    {
        out[i * 2] = hex_digit((u8)(g.bytes[i] >> 4));
        out[i * 2 + 1] = hex_digit((u8)(g.bytes[i] & 0xFu));
    }
    out[32] = '\0';
    return 32;
}

bool mel_guid_from_string(str8 s, Mel_Guid* out)
{
    if (s.len != 32 || s.data == NULL)
        return false;
    Mel_Guid g;
    for (u32 i = 0; i < 16; i++)
    {
        u8 hi, lo;
        if (!hex_value((char)s.data[i * 2], &hi) || !hex_value((char)s.data[i * 2 + 1], &lo))
            return false;
        g.bytes[i] = (u8)((hi << 4) | lo);
    }
    *out = g;
    return true;
}

bool mel_guid_vidpid(Mel_Guid g, u16* out_vendor, u16* out_product, u16* out_version)
{
    u16 vendor = get_le16(&g.bytes[4]);
    u16 product = get_le16(&g.bytes[8]);
    if (vendor == 0 && product == 0)
        return false;
    if (out_vendor)
        *out_vendor = vendor;
    if (out_product)
        *out_product = product;
    if (out_version)
        *out_version = get_le16(&g.bytes[12]);
    return true;
}
