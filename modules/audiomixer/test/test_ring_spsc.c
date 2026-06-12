#include <test/test.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <core/types.h>
#include <thread/thread.h>

typedef struct Mel_Mixer_Ring Mel_Mixer_Ring;

Mel_Mixer_Ring* mel_mixer_ring_create(const Mel_Alloc* a, u32 capacity_samples);
void            mel_mixer_ring_destroy(Mel_Mixer_Ring* r);
u32             mel_mixer_ring_capacity(const Mel_Mixer_Ring* r);
u32             mel_mixer_ring_write_available(const Mel_Mixer_Ring* r);
u32             mel_mixer_ring_read_available(const Mel_Mixer_Ring* r);
u32             mel_mixer_ring_write(Mel_Mixer_Ring* r, const f32* src, u32 count);
u32             mel_mixer_ring_read(Mel_Mixer_Ring* r, f32* dst, u32 count);

static const Mel_Alloc* test_alloc(void) { return mel_alloc_heap(); }

MEL_TEST(ring, bulk_write_then_read_identical)
{
    const Mel_Alloc* a = test_alloc();
    Mel_Mixer_Ring*  r = mel_mixer_ring_create(a, 64u);
    MEL_REQUIRE_NOT_NULL(r);

    MEL_EXPECT_EQ(mel_mixer_ring_capacity(r), 64u);
    MEL_EXPECT_EQ(mel_mixer_ring_read_available(r), 0u);
    MEL_EXPECT_EQ(mel_mixer_ring_write_available(r), 64u);

    f32 in[32];
    for (u32 i = 0; i < 32u; i++)
        in[i] = (f32)i + 0.5f;

    u32 wrote = mel_mixer_ring_write(r, in, 32u);
    MEL_EXPECT_EQ(wrote, 32u);
    MEL_EXPECT_EQ(mel_mixer_ring_read_available(r), 32u);
    MEL_EXPECT_EQ(mel_mixer_ring_write_available(r), 32u);

    f32 out[32];
    u32 read = mel_mixer_ring_read(r, out, 32u);
    MEL_EXPECT_EQ(read, 32u);
    for (u32 i = 0; i < 32u; i++)
        MEL_EXPECT_FLOAT_EQ(out[i], in[i], 0.0f);

    MEL_EXPECT_EQ(mel_mixer_ring_read_available(r), 0u);
    MEL_EXPECT_EQ(mel_mixer_ring_write_available(r), 64u);

    mel_mixer_ring_destroy(r);
}

MEL_TEST(ring, wrap_around_boundary)
{
    const Mel_Alloc* a = test_alloc();
    Mel_Mixer_Ring*  r = mel_mixer_ring_create(a, 8u);
    MEL_REQUIRE_NOT_NULL(r);

    f32 a0[6] = { 0.f, 1.f, 2.f, 3.f, 4.f, 5.f };
    MEL_EXPECT_EQ(mel_mixer_ring_write(r, a0, 6u), 6u);

    f32 d0[4];
    MEL_EXPECT_EQ(mel_mixer_ring_read(r, d0, 4u), 4u);
    for (u32 i = 0; i < 4u; i++)
        MEL_EXPECT_FLOAT_EQ(d0[i], (f32)i, 0.0f);

    f32 a1[6] = { 6.f, 7.f, 8.f, 9.f, 10.f, 11.f };
    MEL_EXPECT_EQ(mel_mixer_ring_write(r, a1, 6u), 6u);

    f32 d1[8];
    MEL_EXPECT_EQ(mel_mixer_ring_read(r, d1, 8u), 8u);
    f32 expect[8] = { 4.f, 5.f, 6.f, 7.f, 8.f, 9.f, 10.f, 11.f };
    for (u32 i = 0; i < 8u; i++)
        MEL_EXPECT_FLOAT_EQ(d1[i], expect[i], 0.0f);

    mel_mixer_ring_destroy(r);
}

MEL_TEST(ring, empty_underruns_to_silence)
{
    const Mel_Alloc* a = test_alloc();
    Mel_Mixer_Ring*  r = mel_mixer_ring_create(a, 16u);
    MEL_REQUIRE_NOT_NULL(r);

    f32 in[4] = { 9.f, 9.f, 9.f, 9.f };
    mel_mixer_ring_write(r, in, 4u);

    f32 out[8];
    for (u32 i = 0; i < 8u; i++)
        out[i] = -1.f;

    u32 read = mel_mixer_ring_read(r, out, 8u);
    MEL_EXPECT_EQ(read, 4u);
    for (u32 i = 0; i < 4u; i++)
        MEL_EXPECT_FLOAT_EQ(out[i], 9.f, 0.0f);
    for (u32 i = 4u; i < 8u; i++)
        MEL_EXPECT_FLOAT_EQ(out[i], 0.0f, 0.0f);

    f32 again[4] = { 7.f, 7.f, 7.f, 7.f };
    u32 got = mel_mixer_ring_read(r, again, 4u);
    MEL_EXPECT_EQ(got, 0u);
    for (u32 i = 0; i < 4u; i++)
        MEL_EXPECT_FLOAT_EQ(again[i], 0.0f, 0.0f);

    mel_mixer_ring_destroy(r);
}

MEL_TEST(ring, full_applies_backpressure)
{
    const Mel_Alloc* a = test_alloc();
    Mel_Mixer_Ring*  r = mel_mixer_ring_create(a, 8u);
    MEL_REQUIRE_NOT_NULL(r);

    f32 in[12];
    for (u32 i = 0; i < 12u; i++)
        in[i] = (f32)i;

    u32 wrote = mel_mixer_ring_write(r, in, 12u);
    MEL_EXPECT_EQ(wrote, 8u);
    MEL_EXPECT_EQ(mel_mixer_ring_write_available(r), 0u);
    MEL_EXPECT_EQ(mel_mixer_ring_read_available(r), 8u);

    u32 wrote2 = mel_mixer_ring_write(r, in, 4u);
    MEL_EXPECT_EQ(wrote2, 0u);

    f32 out[8];
    mel_mixer_ring_read(r, out, 8u);
    for (u32 i = 0; i < 8u; i++)
        MEL_EXPECT_FLOAT_EQ(out[i], (f32)i, 0.0f);

    mel_mixer_ring_destroy(r);
}

#define SPSC_TOTAL    1000000u
#define SPSC_CAPACITY 4096u

typedef struct
{
    Mel_Mixer_Ring* ring;
    u32             total;
} Spsc_Ctx;

static int spsc_producer(void* user)
{
    Spsc_Ctx*   ctx = (Spsc_Ctx*)user;
    const u32   chunk = 257u;
    static f32  buf[257];
    u32         produced = 0;
    while (produced < ctx->total)
    {
        u32 want = ctx->total - produced;
        if (want > chunk)
            want = chunk;
        for (u32 i = 0; i < want; i++)
            buf[i] = (f32)(produced + i);

        u32 off = 0;
        while (off < want)
        {
            u32 n = mel_mixer_ring_write(ctx->ring, buf + off, want - off);
            off += n;
            if (n == 0u)
                mel_thread_yield();
        }
        produced += want;
    }
    return 0;
}

static int spsc_consumer(void* user)
{
    Spsc_Ctx*  ctx = (Spsc_Ctx*)user;
    const u32  chunk = 193u;
    static f32 buf[193];
    u32        consumed = 0;
    int        mismatches = 0;
    while (consumed < ctx->total)
    {
        u32 want = ctx->total - consumed;
        if (want > chunk)
            want = chunk;

        u32 avail = mel_mixer_ring_read_available(ctx->ring);
        if (avail == 0u)
        {
            mel_thread_yield();
            continue;
        }
        u32 take = want < avail ? want : avail;
        u32 got = mel_mixer_ring_read(ctx->ring, buf, take);
        for (u32 i = 0; i < got; i++)
        {
            if (buf[i] != (f32)(consumed + i))
                mismatches++;
        }
        consumed += got;
    }
    return mismatches;
}

MEL_TEST(ring, spsc_threads_no_torn_lost_dup)
{
    const Mel_Alloc* a = test_alloc();
    Mel_Mixer_Ring*  r = mel_mixer_ring_create(a, SPSC_CAPACITY);
    MEL_REQUIRE_NOT_NULL(r);

    Spsc_Ctx ctx = { .ring = r, .total = SPSC_TOTAL };

    Mel_Thread prod;
    Mel_Thread cons;
    MEL_REQUIRE(mel_thread_spawn(&prod, spsc_producer, &ctx, .name = "ring-prod"));
    MEL_REQUIRE(mel_thread_spawn(&cons, spsc_consumer, &ctx, .name = "ring-cons"));

    int prod_code = -1;
    int cons_code = -1;
    MEL_REQUIRE(mel_thread_join(&prod, &prod_code));
    MEL_REQUIRE(mel_thread_join(&cons, &cons_code));

    MEL_EXPECT_EQ(prod_code, 0);
    MEL_EXPECT_EQ(cons_code, 0);
    MEL_EXPECT_EQ(mel_mixer_ring_read_available(r), 0u);

    mel_mixer_ring_destroy(r);
}
