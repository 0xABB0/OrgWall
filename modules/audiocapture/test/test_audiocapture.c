#include <test/test.h>

#include <audiocapture/audiocapture.h>
#include <allocator/heap.h>

#include "../src/audiocapture_internal.h"

MEL_TEST(audiocapture, ring_write_read)
{
    Mel_AC_Ring r;
    mel_ac_ring_init(&r, mel_alloc_heap(), 8);

    f32 in[6] = { 1, 2, 3, 4, 5, 6 };
    MEL_EXPECT_EQ(mel_ac_ring_write(&r, in, 6), 6u);
    MEL_EXPECT_EQ(mel_ac_ring_available(&r), 6u);

    f32 out[6] = { 0 };
    MEL_EXPECT_EQ(mel_ac_ring_read(&r, out, 4), 4u);
    MEL_EXPECT_EQ(out[0], 1.0f);
    MEL_EXPECT_EQ(out[3], 4.0f);
    MEL_EXPECT_EQ(mel_ac_ring_available(&r), 2u);

    mel_ac_ring_free(&r, mel_alloc_heap());
}

MEL_TEST(audiocapture, ring_wraps_and_bounds)
{
    Mel_AC_Ring r;
    mel_ac_ring_init(&r, mel_alloc_heap(), 4);

    f32 in[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    MEL_EXPECT_EQ(mel_ac_ring_write(&r, in, 8), 4u);

    f32 out[4] = { 0 };
    MEL_EXPECT_EQ(mel_ac_ring_read(&r, out, 2), 2u);
    MEL_EXPECT_EQ(mel_ac_ring_write(&r, in + 4, 4), 2u);

    MEL_EXPECT_EQ(mel_ac_ring_read(&r, out, 4), 4u);
    MEL_EXPECT_EQ(out[0], 3.0f);
    MEL_EXPECT_EQ(out[1], 4.0f);
    MEL_EXPECT_EQ(out[2], 5.0f);
    MEL_EXPECT_EQ(out[3], 6.0f);

    mel_ac_ring_free(&r, mel_alloc_heap());
}

MEL_TEST(audiocapture, enumerate_smoke)
{
    u32 ids[32];
    i32 n = mel_audiocapture_enumerate(ids, 32);
    MEL_EXPECT_GE(n, 0);

    for (i32 i = 0; i < n; i++)
    {
        str8 name = mel_audiocapture_device_name(ids[i], mel_alloc_heap());
        if (name.data)
            mel_dealloc(mel_alloc_heap(), name.data);
    }

    u32  def = 0;
    bool has_default = mel_audiocapture_default_device(&def);
    if (n > 0)
        MEL_EXPECT(has_default);

    (void)mel_audiocapture_auth_determined();
}
