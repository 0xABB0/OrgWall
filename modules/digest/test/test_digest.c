#include <test/test.h>

#include <digest/blake2.h>
#include <digest/blake3.h>
#include <digest/hmac.h>
#include <digest/md5.h>
#include <digest/ripemd160.h>
#include <digest/sha1.h>
#include <digest/sha2.h>
#include <digest/sha3.h>
#include <digest/sm3.h>

#include "digest_vectors.h"

#define MEL__DIGEST_FILL_MAX 5000

static void mel__digest_fill(u8* buf, usize len)
{
    for (usize i = 0; i < len; i++)
        buf[i] = (u8)(((u32)(i * 2654435761U)) >> 24);
}

static const usize mel__digest_chunks[] = { 1, 3, 17, 64, 113, 257, 1024 };

#define MEL__DIGEST_VECTOR_TEST(name_, fn_, field_)                         \
    MEL_TEST(digest, name_##_vectors)                                       \
    {                                                                       \
        static u8 buf[MEL__DIGEST_FILL_MAX];                                \
        mel__digest_fill(buf, sizeof(buf));                                 \
        for (usize i = 0; i < (usize)countof(mel__digest_vectors); i++)     \
        {                                                                   \
            const Mel__Digest_Vector* v = &mel__digest_vectors[i];          \
            __typeof__(fn_(buf, 0))   h = fn_(buf, v->len);                 \
            MEL_EXPECT(memcmp(h.bytes, v->field_, sizeof(v->field_)) == 0); \
        }                                                                   \
    }

#define MEL__DIGEST_CHUNK_TEST(name_, fn_, State_, init_, update_, final_)       \
    MEL_TEST(digest, name_##_chunked_matches_oneshot)                            \
    {                                                                            \
        static u8 buf[MEL__DIGEST_FILL_MAX];                                     \
        mel__digest_fill(buf, sizeof(buf));                                      \
        State_ st;                                                               \
        init_(&st);                                                              \
        usize off = 0;                                                           \
        usize ci = 0;                                                            \
        while (off < sizeof(buf))                                                \
        {                                                                        \
            usize take = mel__digest_chunks[ci++ % countof(mel__digest_chunks)]; \
            if (take > sizeof(buf) - off)                                        \
                take = sizeof(buf) - off;                                        \
            update_(&st, buf + off, take);                                       \
            off += take;                                                         \
        }                                                                        \
        __typeof__(fn_(buf, 0)) a = final_(&st);                                 \
        __typeof__(fn_(buf, 0)) b = fn_(buf, sizeof(buf));                       \
        MEL_EXPECT(memcmp(a.bytes, b.bytes, sizeof(a.bytes)) == 0);              \
        __typeof__(fn_(buf, 0)) again = final_(&st);                             \
        MEL_EXPECT(memcmp(again.bytes, b.bytes, sizeof(b.bytes)) == 0);          \
    }

MEL__DIGEST_VECTOR_TEST(md5, mel_md5, md5)
MEL__DIGEST_VECTOR_TEST(sha1, mel_sha1, sha1)
MEL__DIGEST_VECTOR_TEST(sha224, mel_sha224, sha224)
MEL__DIGEST_VECTOR_TEST(sha256, mel_sha256, sha256)
MEL__DIGEST_VECTOR_TEST(sha384, mel_sha384, sha384)
MEL__DIGEST_VECTOR_TEST(sha512, mel_sha512, sha512)
MEL__DIGEST_VECTOR_TEST(sha512_224, mel_sha512_224, sha512_224)
MEL__DIGEST_VECTOR_TEST(sha512_256, mel_sha512_256, sha512_256)
MEL__DIGEST_VECTOR_TEST(sha3_224, mel_sha3_224, sha3_224)
MEL__DIGEST_VECTOR_TEST(sha3_256, mel_sha3_256, sha3_256)
MEL__DIGEST_VECTOR_TEST(sha3_384, mel_sha3_384, sha3_384)
MEL__DIGEST_VECTOR_TEST(sha3_512, mel_sha3_512, sha3_512)
MEL__DIGEST_VECTOR_TEST(ripemd160, mel_ripemd160, ripemd160)
MEL__DIGEST_VECTOR_TEST(sm3, mel_sm3, sm3)

MEL__DIGEST_CHUNK_TEST(md5, mel_md5, Mel_Md5_State, mel_md5_init, mel_md5_update, mel_md5_final)
MEL__DIGEST_CHUNK_TEST(sha1, mel_sha1, Mel_Sha1_State, mel_sha1_init, mel_sha1_update, mel_sha1_final)
MEL__DIGEST_CHUNK_TEST(sha224, mel_sha224, Mel_Sha256_State, mel_sha224_init, mel_sha256_update, mel_sha224_final)
MEL__DIGEST_CHUNK_TEST(sha256, mel_sha256, Mel_Sha256_State, mel_sha256_init, mel_sha256_update, mel_sha256_final)
MEL__DIGEST_CHUNK_TEST(sha384, mel_sha384, Mel_Sha512_State, mel_sha384_init, mel_sha512_update, mel_sha384_final)
MEL__DIGEST_CHUNK_TEST(sha512, mel_sha512, Mel_Sha512_State, mel_sha512_init, mel_sha512_update, mel_sha512_final)
MEL__DIGEST_CHUNK_TEST(sha512_224, mel_sha512_224, Mel_Sha512_State, mel_sha512_224_init, mel_sha512_update, mel_sha512_224_final)
MEL__DIGEST_CHUNK_TEST(sha512_256, mel_sha512_256, Mel_Sha512_State, mel_sha512_256_init, mel_sha512_update, mel_sha512_256_final)
MEL__DIGEST_CHUNK_TEST(sha3_224, mel_sha3_224, Mel_Sha3_State, mel_sha3_224_init, mel_sha3_update, mel_sha3_224_final)
MEL__DIGEST_CHUNK_TEST(sha3_256, mel_sha3_256, Mel_Sha3_State, mel_sha3_256_init, mel_sha3_update, mel_sha3_256_final)
MEL__DIGEST_CHUNK_TEST(sha3_384, mel_sha3_384, Mel_Sha3_State, mel_sha3_384_init, mel_sha3_update, mel_sha3_384_final)
MEL__DIGEST_CHUNK_TEST(sha3_512, mel_sha3_512, Mel_Sha3_State, mel_sha3_512_init, mel_sha3_update, mel_sha3_512_final)
MEL__DIGEST_CHUNK_TEST(ripemd160, mel_ripemd160, Mel_Ripemd160_State, mel_ripemd160_init, mel_ripemd160_update, mel_ripemd160_final)
MEL__DIGEST_CHUNK_TEST(sm3, mel_sm3, Mel_Sm3_State, mel_sm3_init, mel_sm3_update, mel_sm3_final)

MEL_TEST(digest, shake_vectors)
{
    static u8 buf[MEL__DIGEST_FILL_MAX];
    mel__digest_fill(buf, sizeof(buf));
    for (usize i = 0; i < (usize)countof(mel__digest_vectors); i++)
    {
        const Mel__Digest_Vector* v = &mel__digest_vectors[i];
        u8                        out128[32];
        u8                        out256[64];
        mel_shake128(buf, v->len, out128, sizeof(out128));
        mel_shake256(buf, v->len, out256, sizeof(out256));
        MEL_EXPECT(memcmp(out128, v->shake128_32, sizeof(out128)) == 0);
        MEL_EXPECT(memcmp(out256, v->shake256_64, sizeof(out256)) == 0);
    }
}

MEL_TEST(digest, shake_long_output_crosses_rate)
{
    static u8 buf[MEL__DIGEST_FILL_MAX];
    mel__digest_fill(buf, sizeof(buf));
    u8 out[200];
    mel_shake128(buf, 129, out, sizeof(out));
    MEL_EXPECT(memcmp(out, mel__shake128_200, sizeof(out)) == 0);
    mel_shake256(buf, 129, out, sizeof(out));
    MEL_EXPECT(memcmp(out, mel__shake256_200, sizeof(out)) == 0);
}

MEL_TEST(digest, shake_chunked_squeeze_matches_oneshot)
{
    static u8 buf[MEL__DIGEST_FILL_MAX];
    mel__digest_fill(buf, sizeof(buf));

    Mel_Shake_State st;
    mel_shake256_init(&st);
    usize off = 0;
    usize ci = 0;
    while (off < sizeof(buf))
    {
        usize take = mel__digest_chunks[ci++ % countof(mel__digest_chunks)];
        if (take > sizeof(buf) - off)
            take = sizeof(buf) - off;
        mel_shake_update(&st, buf + off, take);
        off += take;
    }

    u8 pieces[200];
    mel_shake_squeeze(&st, pieces, 1);
    mel_shake_squeeze(&st, pieces + 1, 67);
    mel_shake_squeeze(&st, pieces + 68, 132);

    u8 oneshot[200];
    mel_shake256(buf, sizeof(buf), oneshot, sizeof(oneshot));
    MEL_EXPECT(memcmp(pieces, oneshot, sizeof(oneshot)) == 0);
}

MEL_TEST(digest, blake2b_vectors)
{
    static u8 buf[MEL__DIGEST_FILL_MAX];
    mel__digest_fill(buf, sizeof(buf));
    u8 key[64];
    for (usize i = 0; i < sizeof(key); i++)
        key[i] = (u8)i;

    for (usize i = 0; i < (usize)countof(mel__digest_vectors); i++)
    {
        const Mel__Digest_Vector* v = &mel__digest_vectors[i];
        u8                        out[64];
        mel_blake2b(out, sizeof(out), buf, v->len, NULL, 0);
        MEL_EXPECT(memcmp(out, v->blake2b_64, sizeof(out)) == 0);
        mel_blake2b(out, sizeof(out), buf, v->len, key, sizeof(key));
        MEL_EXPECT(memcmp(out, v->blake2b_keyed_64, sizeof(out)) == 0);
    }

    for (usize i = 0; i < (usize)countof(mel__blake2b_cut_lens); i++)
    {
        u8 out[64];
        mel_blake2b(out, mel__blake2b_cut_lens[i], buf, 129, NULL, 0);
        MEL_EXPECT(memcmp(out, mel__blake2b_cuts[i], mel__blake2b_cut_lens[i]) == 0);
    }
}

MEL_TEST(digest, blake2s_vectors)
{
    static u8 buf[MEL__DIGEST_FILL_MAX];
    mel__digest_fill(buf, sizeof(buf));
    u8 key[32];
    for (usize i = 0; i < sizeof(key); i++)
        key[i] = (u8)i;

    for (usize i = 0; i < (usize)countof(mel__digest_vectors); i++)
    {
        const Mel__Digest_Vector* v = &mel__digest_vectors[i];
        u8                        out[32];
        mel_blake2s(out, sizeof(out), buf, v->len, NULL, 0);
        MEL_EXPECT(memcmp(out, v->blake2s_32, sizeof(out)) == 0);
        mel_blake2s(out, sizeof(out), buf, v->len, key, sizeof(key));
        MEL_EXPECT(memcmp(out, v->blake2s_keyed_32, sizeof(out)) == 0);
    }

    for (usize i = 0; i < (usize)countof(mel__blake2s_cut_lens); i++)
    {
        u8 out[32];
        mel_blake2s(out, mel__blake2s_cut_lens[i], buf, 129, NULL, 0);
        MEL_EXPECT(memcmp(out, mel__blake2s_cuts[i], mel__blake2s_cut_lens[i]) == 0);
    }
}

MEL_TEST(digest, blake2_chunked_matches_oneshot)
{
    static u8 buf[MEL__DIGEST_FILL_MAX];
    mel__digest_fill(buf, sizeof(buf));

    Mel_Blake2b_State bst;
    mel_blake2b_init(&bst, 64, NULL, 0);
    Mel_Blake2s_State sst;
    mel_blake2s_init(&sst, 32, NULL, 0);

    usize off = 0;
    usize ci = 0;
    while (off < sizeof(buf))
    {
        usize take = mel__digest_chunks[ci++ % countof(mel__digest_chunks)];
        if (take > sizeof(buf) - off)
            take = sizeof(buf) - off;
        mel_blake2b_update(&bst, buf + off, take);
        mel_blake2s_update(&sst, buf + off, take);
        off += take;
    }

    u8 a[64], b[64];
    mel_blake2b_final(&bst, a);
    mel_blake2b(b, 64, buf, sizeof(buf), NULL, 0);
    MEL_EXPECT(memcmp(a, b, 64) == 0);

    u8 c[32], d[32];
    mel_blake2s_final(&sst, c);
    mel_blake2s(d, 32, buf, sizeof(buf), NULL, 0);
    MEL_EXPECT(memcmp(c, d, 32) == 0);
}

#define MEL__BLAKE3_INPUT_MAX 102400

static void mel__blake3_fill(u8* buf, usize len)
{
    for (usize i = 0; i < len; i++)
        buf[i] = (u8)(i % 251);
}

MEL_TEST(digest, blake3_reference_vectors)
{
    static u8 buf[MEL__BLAKE3_INPUT_MAX];
    mel__blake3_fill(buf, sizeof(buf));

    for (usize i = 0; i < (usize)countof(mel__blake3_vectors); i++)
    {
        const Mel__Blake3_Vector* v = &mel__blake3_vectors[i];
        u8                        out[131];

        mel_blake3(buf, v->len, out, sizeof(out));
        MEL_EXPECT(memcmp(out, v->hash, sizeof(out)) == 0);

        mel_blake3_keyed((const u8*)mel__blake3_key, buf, v->len, out, sizeof(out));
        MEL_EXPECT(memcmp(out, v->keyed, sizeof(out)) == 0);

        mel_blake3_derive_key(mel__blake3_context, lengthof(mel__blake3_context), buf, v->len, out, sizeof(out));
        MEL_EXPECT(memcmp(out, v->derive, sizeof(out)) == 0);
    }
}

MEL_TEST(digest, blake3_chunked_matches_oneshot)
{
    static u8 buf[MEL__BLAKE3_INPUT_MAX];
    mel__blake3_fill(buf, sizeof(buf));

    Mel_Blake3_State st;
    mel_blake3_init(&st);
    usize off = 0;
    usize ci = 0;
    while (off < sizeof(buf))
    {
        usize take = mel__digest_chunks[ci++ % countof(mel__digest_chunks)];
        if (take > sizeof(buf) - off)
            take = sizeof(buf) - off;
        mel_blake3_update(&st, buf + off, take);
        off += take;
    }

    u8 a[131], b[131];
    mel_blake3_final(&st, a, sizeof(a));
    mel_blake3(buf, sizeof(buf), b, sizeof(b));
    MEL_EXPECT(memcmp(a, b, sizeof(a)) == 0);

    u8 again[131];
    mel_blake3_final(&st, again, sizeof(again));
    MEL_EXPECT(memcmp(again, b, sizeof(b)) == 0);
}

MEL_TEST(digest, digest_eq)
{
    u8 a[32], b[32];
    memset(a, 0xab, sizeof(a));
    memset(b, 0xab, sizeof(b));
    MEL_EXPECT(mel_digest_eq(a, b, sizeof(a)));
    b[17] ^= 1;
    MEL_EXPECT(!mel_digest_eq(a, b, sizeof(a)));
    MEL_EXPECT(mel_digest_eq(a, b, 0));
}

typedef struct Mel__Hmac_Vector
{
    const u8* key;
    usize     key_len;
    const u8* msg;
    usize     msg_len;
    u8        sha256[32];
    u8        sha512[64];
} Mel__Hmac_Vector;

MEL_TEST(digest, hmac_sha256_rfc4231)
{
    static const u8 k1[] = { 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b };
    static const u8 m1[] = { 'H', 'i', ' ', 'T', 'h', 'e', 'r', 'e' };
    static const u8 exp1[] = { 0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53, 0x5c, 0xa8, 0xaf, 0xce, 0xaf, 0x0b, 0xf1, 0x2b, 0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83, 0x3d, 0xa7, 0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7 };

    static const u8 k2[] = { 'J', 'e', 'f', 'e' };
    static const u8 m2[] = { 'w', 'h', 'a', 't', ' ', 'd', 'o', ' ', 'y', 'a', ' ', 'w', 'a', 'n', 't', ' ', 'f', 'o', 'r', ' ', 'n', 'o', 't', 'h', 'i', 'n', 'g', '?' };
    static const u8 exp2[] = { 0x5b, 0xdc, 0xc1, 0x46, 0xbf, 0x60, 0x75, 0x4e, 0x6a, 0x04, 0x24, 0x26, 0x08, 0x95, 0x75, 0xc7, 0x5a, 0x00, 0x3f, 0x08, 0x9d, 0x27, 0x39, 0x83, 0x9d, 0xec, 0x58, 0xb9, 0x64, 0xec, 0x38, 0x43 };

    static const u8 k3[] = { 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa };
    static const u8 m3[50] = { [0 ... 49] = 0xdd };
    static const u8 exp3[] = { 0x77, 0x3e, 0xa9, 0x1e, 0x36, 0x80, 0x0e, 0x46, 0x85, 0x4d, 0xb8, 0xeb, 0xd0, 0x91, 0x81, 0xa7, 0x29, 0x59, 0x09, 0x8b, 0x3e, 0xf8, 0xc1, 0x22, 0xd9, 0x63, 0x55, 0x14, 0xce, 0xd5, 0x65, 0xfe };

    u8 long_key[131];
    for (int i = 0; i < 131; i++)
        long_key[i] = (u8)i;
    static const u8 m4[] = "Test Using Larger Than Block-Size Key - Hash Key First";
    static const u8 exp4[] = { 0xd3, 0xa7, 0xe1, 0x84, 0x55, 0xd6, 0x0d, 0xc2, 0x77, 0x08, 0x32, 0xb7, 0x37, 0x3c, 0x29, 0x92, 0x77, 0x45, 0x97, 0x6a, 0x2c, 0xf1, 0xc0, 0x04, 0x0e, 0xe1, 0x1d, 0x06, 0x88, 0x40, 0x6a, 0x30 };

    u8 out[32];
    mel_hmac_sha256(k1, sizeof(k1), m1, sizeof(m1), out);
    MEL_EXPECT(memcmp(out, exp1, 32) == 0);

    mel_hmac_sha256(k2, sizeof(k2), m2, sizeof(m2), out);
    MEL_EXPECT(memcmp(out, exp2, 32) == 0);

    mel_hmac_sha256(k3, sizeof(k3), m3, sizeof(m3), out);
    MEL_EXPECT(memcmp(out, exp3, 32) == 0);

    mel_hmac_sha256(long_key, sizeof(long_key), m4, sizeof(m4) - 1, out);
    MEL_EXPECT(memcmp(out, exp4, 32) == 0);
}

MEL_TEST(digest, hmac_sha512_rfc4231)
{
    static const u8 k1[] = { 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b };
    static const u8 m1[] = { 'H', 'i', ' ', 'T', 'h', 'e', 'r', 'e' };
    static const u8 exp1[] = {
        0x87, 0xaa, 0x7c, 0xde, 0xa5, 0xef, 0x61, 0x9d, 0x4f, 0xf0, 0xb4, 0x24, 0x1a, 0x1d, 0x6c, 0xb0, 0x23, 0x79, 0xf4, 0xe2, 0xce, 0x4e, 0xc2, 0x78, 0x7a, 0xd0, 0xb3, 0x05, 0x45, 0xe1, 0x7c, 0xde,
        0xda, 0xa8, 0x33, 0xb7, 0xd6, 0xb8, 0xa7, 0x02, 0x03, 0x8b, 0x27, 0x4e, 0xae, 0xa3, 0xf4, 0xe4, 0xbe, 0x9d, 0x91, 0x4e, 0xeb, 0x61, 0xf1, 0x70, 0x2e, 0x69, 0x6c, 0x20, 0x3a, 0x12, 0x68, 0x54,
    };

    static const u8 k2[] = { 'J', 'e', 'f', 'e' };
    static const u8 m2[] = { 'w', 'h', 'a', 't', ' ', 'd', 'o', ' ', 'y', 'a', ' ', 'w', 'a', 'n', 't', ' ', 'f', 'o', 'r', ' ', 'n', 'o', 't', 'h', 'i', 'n', 'g', '?' };
    static const u8 exp2[] = {
        0x16, 0x4b, 0x7a, 0x7b, 0xfc, 0xf8, 0x19, 0xe2, 0xe3, 0x95, 0xfb, 0xe7, 0x3b, 0x56, 0xe0, 0xa3, 0x87, 0xbd, 0x64, 0x22, 0x2e, 0x83, 0x1f, 0xd6, 0x10, 0x27, 0x0c, 0xd7, 0xea, 0x25, 0x05, 0x54,
        0x97, 0x58, 0xbf, 0x75, 0xc0, 0x5a, 0x99, 0x4a, 0x6d, 0x03, 0x4f, 0x65, 0xf8, 0xf0, 0xe6, 0xfd, 0xca, 0xea, 0xb1, 0xa3, 0x4d, 0x4a, 0x6b, 0x4b, 0x63, 0x6e, 0x07, 0x0a, 0x38, 0xbc, 0xe7, 0x37,
    };

    u8 long_key[131];
    for (int i = 0; i < 131; i++)
        long_key[i] = (u8)i;
    static const u8 m3[] = "Test Using Larger Than Block-Size Key - Hash Key First";
    static const u8 exp3[] = {
        0x74, 0x1d, 0xf3, 0xf4, 0x41, 0x43, 0xd5, 0x90, 0x10, 0x98, 0x07, 0xa8, 0x49, 0x13, 0x16, 0xb7, 0xd9, 0xc6, 0x6f, 0x6e, 0x9b, 0xb6, 0x77, 0x67, 0x24, 0x1d, 0xd2, 0x69, 0xb4, 0xe8, 0x54, 0xc4,
        0x68, 0x28, 0xb9, 0x08, 0xfc, 0x24, 0x9c, 0x30, 0xd0, 0x7c, 0x67, 0x52, 0x2a, 0x1e, 0x8d, 0x24, 0xbe, 0xa7, 0x53, 0x23, 0xcd, 0xb8, 0x93, 0xeb, 0xe1, 0x0f, 0xde, 0x92, 0x98, 0x34, 0x90, 0x7f,
    };

    u8 out[64];
    mel_hmac_sha512(k1, sizeof(k1), m1, sizeof(m1), out);
    MEL_EXPECT(memcmp(out, exp1, 64) == 0);

    mel_hmac_sha512(k2, sizeof(k2), m2, sizeof(m2), out);
    MEL_EXPECT(memcmp(out, exp2, 64) == 0);

    mel_hmac_sha512(long_key, sizeof(long_key), m3, sizeof(m3) - 1, out);
    MEL_EXPECT(memcmp(out, exp3, 64) == 0);
}
