#include <test/test.h>

#include <pcm/ring.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <core/types.h>
#include <thread/thread.h>

#include <stdatomic.h>

MEL_TEST(pcm_ring, create_reports_constants)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Pcm_Ring*    r = mel_pcm_ring_create(a, 2u, 64u);
    MEL_REQUIRE_NOT_NULL(r);

    MEL_EXPECT_EQ(mel_pcm_ring_channels(r), 2u);
    MEL_EXPECT_EQ(mel_pcm_ring_capacity(r), 64u);
    MEL_EXPECT_EQ(mel_pcm_ring_read_available(r), 0u);
    MEL_EXPECT_EQ(mel_pcm_ring_write_available(r), 64u);

    mel_pcm_ring_destroy(r);
}

MEL_TEST(pcm_ring, write_then_read_identical_stereo)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Pcm_Ring*    r = mel_pcm_ring_create(a, 2u, 64u);
    MEL_REQUIRE_NOT_NULL(r);

    f32 in[32u * 2u];
    for (u32 i = 0; i < 32u; i++)
    {
        in[i * 2u + 0u] = (f32)i + 0.25f;
        in[i * 2u + 1u] = -((f32)i + 0.75f);
    }

    MEL_EXPECT_EQ(mel_pcm_ring_write(r, in, 32u), 32u);
    MEL_EXPECT_EQ(mel_pcm_ring_read_available(r), 32u);
    MEL_EXPECT_EQ(mel_pcm_ring_write_available(r), 32u);

    f32 out[32u * 2u];
    MEL_EXPECT_EQ(mel_pcm_ring_read(r, out, 32u), 32u);
    for (u32 i = 0; i < 32u * 2u; i++)
        MEL_EXPECT_FLOAT_EQ(out[i], in[i], 0.0f);

    MEL_EXPECT_EQ(mel_pcm_ring_read_available(r), 0u);
    MEL_EXPECT_EQ(mel_pcm_ring_write_available(r), 64u);

    mel_pcm_ring_destroy(r);
}

MEL_TEST(pcm_ring, full_ring_rejects_excess)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Pcm_Ring*    r = mel_pcm_ring_create(a, 1u, 8u);
    MEL_REQUIRE_NOT_NULL(r);

    f32 in[12];
    for (u32 i = 0; i < 12u; i++)
        in[i] = (f32)i;

    MEL_EXPECT_EQ(mel_pcm_ring_write(r, in, 12u), 8u);
    MEL_EXPECT_EQ(mel_pcm_ring_write(r, in, 1u), 0u);
    MEL_EXPECT_EQ(mel_pcm_ring_write_available(r), 0u);

    f32 out[8];
    MEL_EXPECT_EQ(mel_pcm_ring_read(r, out, 8u), 8u);
    for (u32 i = 0; i < 8u; i++)
        MEL_EXPECT_FLOAT_EQ(out[i], in[i], 0.0f);

    mel_pcm_ring_destroy(r);
}

MEL_TEST(pcm_ring, partial_read_returns_available)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Pcm_Ring*    r = mel_pcm_ring_create(a, 1u, 16u);
    MEL_REQUIRE_NOT_NULL(r);

    f32 in[4] = { 1.f, 2.f, 3.f, 4.f };
    MEL_EXPECT_EQ(mel_pcm_ring_write(r, in, 4u), 4u);

    f32 out[16];
    MEL_EXPECT_EQ(mel_pcm_ring_read(r, out, 16u), 4u);
    for (u32 i = 0; i < 4u; i++)
        MEL_EXPECT_FLOAT_EQ(out[i], in[i], 0.0f);

    MEL_EXPECT_EQ(mel_pcm_ring_read(r, out, 16u), 0u);

    mel_pcm_ring_destroy(r);
}

MEL_TEST(pcm_ring, wrap_around_preserves_frames)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Pcm_Ring*    r = mel_pcm_ring_create(a, 2u, 8u);
    MEL_REQUIRE_NOT_NULL(r);

    f32 block[6u * 2u];
    for (u32 i = 0; i < 6u; i++)
    {
        block[i * 2u + 0u] = (f32)i;
        block[i * 2u + 1u] = (f32)i + 100.f;
    }
    MEL_EXPECT_EQ(mel_pcm_ring_write(r, block, 6u), 6u);

    f32 out[8u * 2u];
    MEL_EXPECT_EQ(mel_pcm_ring_read(r, out, 4u), 4u);
    for (u32 i = 0; i < 4u; i++)
    {
        MEL_EXPECT_FLOAT_EQ(out[i * 2u + 0u], (f32)i, 0.0f);
        MEL_EXPECT_FLOAT_EQ(out[i * 2u + 1u], (f32)i + 100.f, 0.0f);
    }

    for (u32 i = 0; i < 6u; i++)
    {
        block[i * 2u + 0u] = (f32)(i + 6u);
        block[i * 2u + 1u] = (f32)(i + 6u) + 100.f;
    }
    MEL_EXPECT_EQ(mel_pcm_ring_write(r, block, 6u), 6u);

    MEL_EXPECT_EQ(mel_pcm_ring_read(r, out, 8u), 8u);
    for (u32 i = 0; i < 8u; i++)
    {
        MEL_EXPECT_FLOAT_EQ(out[i * 2u + 0u], (f32)(i + 4u), 0.0f);
        MEL_EXPECT_FLOAT_EQ(out[i * 2u + 1u], (f32)(i + 4u) + 100.f, 0.0f);
    }

    mel_pcm_ring_destroy(r);
}

MEL_TEST(pcm_ring, non_power_of_two_capacity)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Pcm_Ring*    r = mel_pcm_ring_create(a, 1u, 7u);
    MEL_REQUIRE_NOT_NULL(r);

    f32 next = 0.f;
    f32 expect = 0.f;
    for (u32 round = 0; round < 100u; round++)
    {
        f32 in[5];
        for (u32 i = 0; i < 5u; i++)
            in[i] = next++;
        MEL_REQUIRE_EQ(mel_pcm_ring_write(r, in, 5u), 5u);

        f32 out[5];
        MEL_REQUIRE_EQ(mel_pcm_ring_read(r, out, 5u), 5u);
        for (u32 i = 0; i < 5u; i++)
            MEL_REQUIRE_FLOAT_EQ(out[i], expect++, 0.0f);
    }

    mel_pcm_ring_destroy(r);
}

#define STRESS_FRAMES 200000u

typedef struct
{
    Mel_Pcm_Ring* ring;
} Stress_Producer;

static int stress_produce(void* user)
{
    Stress_Producer* p = user;

    f32 block[64u * 2u];
    u32 produced = 0;
    while (produced < STRESS_FRAMES)
    {
        u32 want = STRESS_FRAMES - produced;
        if (want > 64u)
            want = 64u;
        for (u32 i = 0; i < want; i++)
        {
            block[i * 2u + 0u] = (f32)(produced + i);
            block[i * 2u + 1u] = -(f32)(produced + i);
        }

        u32 offset = 0;
        while (offset < want)
        {
            u32 accepted = mel_pcm_ring_write(p->ring, block + (usize)offset * 2u, want - offset);
            offset += accepted;
            if (accepted == 0u)
                mel_thread_yield();
        }
        produced += want;
    }
    return 0;
}

MEL_TEST(pcm_ring, spsc_stress_preserves_sequence)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Pcm_Ring*    r = mel_pcm_ring_create(a, 2u, 1024u);
    MEL_REQUIRE_NOT_NULL(r);

    Stress_Producer p = { .ring = r };
    Mel_Thread      t;
    MEL_REQUIRE(mel_thread_spawn(&t, stress_produce, &p, .name = "pcm-stress"));

    f32 out[96u * 2u];
    u32 consumed = 0;
    u32 corrupt = 0;
    while (consumed < STRESS_FRAMES)
    {
        u32 got = mel_pcm_ring_read(r, out, 96u);
        if (got == 0u)
        {
            mel_thread_yield();
            continue;
        }
        for (u32 i = 0; i < got; i++)
        {
            if (out[i * 2u + 0u] != (f32)(consumed + i) || out[i * 2u + 1u] != -(f32)(consumed + i))
                corrupt++;
        }
        consumed += got;
    }

    MEL_EXPECT_EQ(corrupt, 0u);
    MEL_EXPECT_EQ(consumed, STRESS_FRAMES);
    MEL_EXPECT_EQ(mel_pcm_ring_read_available(r), 0u);

    mel_thread_join(&t, NULL);
    mel_pcm_ring_destroy(r);
}
