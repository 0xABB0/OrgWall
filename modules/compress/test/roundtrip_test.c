#include <compress/compress.h>
#include <compress/brotli.h>
#include <compress/deflate.h>
#include <compress/lz4.h>
#include <compress/rle.h>
#include <compress/zstd.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <string/str8.h>
#include <test/test.h>

#include <string.h>

#define PAYLOAD_RANDOM_LEN     65536
#define PAYLOAD_REPETITIVE_LEN 50000
#define PUMP_IN_CHUNK          3
#define PUMP_OUT_WINDOW        7

static u32 lcg_next(u32* state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static str8 make_random(const Mel_Alloc* alloc, usize len)
{
    u8* p = mel_alloc(alloc, len);
    u32 s = 0xC0FFEEu;
    for (usize i = 0; i < len; i++)
        p[i] = (u8)(lcg_next(&s) >> 24);
    return str8_from_parts(p, (size)len);
}

static str8 make_repetitive(const Mel_Alloc* alloc, usize len)
{
    u8* p = mel_alloc(alloc, len);
    for (usize i = 0; i < len; i++)
        p[i] = (u8)("aaaaaaaaaaaaaaaabcbcbcbc"[i % 24]);
    return str8_from_parts(p, (size)len);
}

static void roundtrip_one(const Mel_Compress_Codec* c, str8 input, u32 level)
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    Mel_Compress_Result packed = mel_compress(c, input, .level = level, .alloc = alloc);
    MEL_REQUIRE(mel_compress_status_ok(packed.status));

    Mel_Compress_Result plain = mel_decompress(c, str8_from_parts(packed.data, (size)packed.len), .alloc = alloc);
    MEL_REQUIRE(mel_compress_status_ok(plain.status));
    MEL_REQUIRE_EQ(plain.len, (usize)input.len);
    if (input.len > 0)
        MEL_EXPECT_EQ(memcmp(plain.data, input.data, (usize)input.len), 0);

    mel_compress_result_free(&packed, alloc);
    mel_compress_result_free(&plain, alloc);
}

static void roundtrip_all_payloads(const Mel_Compress_Codec* c)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    str8             random = make_random(alloc, PAYLOAD_RANDOM_LEN);
    str8             repetitive = make_repetitive(alloc, PAYLOAD_REPETITIVE_LEN);

    roundtrip_one(c, STR8_EMPTY, c->level_default);
    roundtrip_one(c, S8("x"), c->level_default);
    roundtrip_one(c, S8("the quick brown fox jumps over the lazy dog, twice over: the quick brown fox jumps over the lazy dog"), c->level_default);
    roundtrip_one(c, repetitive, c->level_default);
    roundtrip_one(c, random, c->level_default);
    roundtrip_one(c, repetitive, c->level_min);
    if (c->level_max != c->level_default)
        roundtrip_one(c, repetitive, c->level_max);

    mel_dealloc(alloc, random.data);
    mel_dealloc(alloc, repetitive.data);
}

static void pump_tiny_windows(const Mel_Compress_Codec* c)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    str8             input = make_repetitive(alloc, 4096);

    Mel_Compress_Result packed = mel_compress(c, input, .level = c->level_default, .alloc = alloc);
    MEL_REQUIRE(mel_compress_status_ok(packed.status));

    Mel_Compress_Status  st = MEL_COMPRESS_OK;
    Mel_Compress_Stream* s = c->begin((Mel_Compress_Begin){ .decompress = true, .alloc = alloc }, &st);
    MEL_REQUIRE(s != NULL);

    usize cap = (usize)input.len + 64;
    u8*   out = mel_alloc(alloc, cap);
    usize produced = 0;
    usize consumed = 0;
    bool  finished = false;
    u32   guard = 0;
    while (!finished)
    {
        MEL_REQUIRE_LT(guard++, 1000000u);
        usize in_left = packed.len - consumed;
        usize in_chunk = in_left < PUMP_IN_CHUNK ? in_left : PUMP_IN_CHUNK;
        usize room = cap - produced;
        usize window = room < PUMP_OUT_WINDOW ? room : PUMP_OUT_WINDOW;
        str8  in = str8_from_parts(packed.data + consumed, (size)in_chunk);

        Mel_Compress_Step step = c->step(s, in, consumed + in_chunk == packed.len, out + produced, window);
        MEL_REQUIRE(!mel_compress_status_failed(step.status));
        consumed += step.in_consumed;
        produced += step.out_produced;
        finished = step.finished;
    }
    c->end(s);

    MEL_REQUIRE_EQ(produced, (usize)input.len);
    MEL_EXPECT_EQ(memcmp(out, input.data, produced), 0);

    mel_dealloc(alloc, out);
    mel_compress_result_free(&packed, alloc);
    mel_dealloc(alloc, input.data);
}

static void reject_truncated(const Mel_Compress_Codec* c)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    str8             input = make_repetitive(alloc, 4096);

    Mel_Compress_Result packed = mel_compress(c, input, .level = c->level_default, .alloc = alloc);
    MEL_REQUIRE(mel_compress_status_ok(packed.status));
    MEL_REQUIRE_GT(packed.len, (usize)4);

    str8                cut = str8_from_parts(packed.data, (size)(packed.len - 3));
    Mel_Compress_Result plain = mel_decompress(c, cut, .alloc = alloc);
    MEL_EXPECT(mel_compress_status_failed(plain.status));

    mel_compress_result_free(&plain, alloc);
    mel_compress_result_free(&packed, alloc);
    mel_dealloc(alloc, input.data);
}

static void reject_bad_level(const Mel_Compress_Codec* c)
{
    const Mel_Alloc*    alloc = mel_alloc_heap();
    Mel_Compress_Result r = mel_compress(c, S8("payload"), .level = c->level_max + 1, .alloc = alloc);
    MEL_EXPECT(mel_compress_status_failed(r.status));
    MEL_EXPECT(r.status & MEL_COMPRESS_BAD_LEVEL);
    mel_compress_result_free(&r, alloc);
}

#define CODEC_SUITE(name_, getter_)                                                 \
    MEL_TEST(compress, name_##_roundtrip) { roundtrip_all_payloads(getter_()); }    \
    MEL_TEST(compress, name_##_pump_tiny_windows) { pump_tiny_windows(getter_()); } \
    MEL_TEST(compress, name_##_reject_truncated) { reject_truncated(getter_()); }   \
    MEL_TEST(compress, name_##_reject_bad_level) { reject_bad_level(getter_()); }

CODEC_SUITE(rle, mel_compress_rle)
CODEC_SUITE(deflate, mel_compress_deflate)
CODEC_SUITE(gzip, mel_compress_gzip)
CODEC_SUITE(lz4, mel_compress_lz4)
CODEC_SUITE(zstd, mel_compress_zstd)
CODEC_SUITE(brotli, mel_compress_brotli)

MEL_TEST(compress, registry_find_sniff_ext)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    mel_compress_registry_init(alloc);
    mel_compress_register(mel_compress_rle());
    mel_compress_register(mel_compress_gzip());
    mel_compress_register(mel_compress_lz4());
    mel_compress_register(mel_compress_zstd());
    mel_compress_register(mel_compress_brotli());
    mel_compress_register(mel_compress_deflate());

    MEL_EXPECT_EQ(mel_compress_count(), (usize)6);
    MEL_EXPECT(mel_compress_find(S8("zstd")) == mel_compress_zstd());
    MEL_EXPECT(mel_compress_find(S8("nope")) == NULL);
    MEL_EXPECT(mel_compress_for_ext(S8("br")) == mel_compress_brotli());
    MEL_EXPECT(mel_compress_for_ext(S8("GZ")) == mel_compress_gzip());

    str8 ids[] = { S8("rle"), S8("gzip"), S8("lz4"), S8("zstd"), S8("deflate") };
    for (usize i = 0; i < sizeof ids / sizeof ids[0]; i++)
    {
        const Mel_Compress_Codec* c = mel_compress_find(ids[i]);
        MEL_REQUIRE(c != NULL);
        Mel_Compress_Result packed = mel_compress(c, S8("sniff me, i dare you"), .level = c->level_default, .alloc = alloc);
        MEL_REQUIRE(mel_compress_status_ok(packed.status));
        MEL_EXPECT(mel_compress_sniff(str8_from_parts(packed.data, (size)packed.len)) == c);
        mel_compress_result_free(&packed, alloc);
    }

    mel_compress_registry_shutdown();
}
