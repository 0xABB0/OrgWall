#include <pcm/ring.h>
#include <pcm/resample.h>
#include <pcm/convert.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <thread/thread.h>

#include <stdatomic.h>

#define BLOCK_FRAMES 256u
#define READ_FRAMES  512u

typedef struct
{
    Mel_Pcm_Ring* ring;
    _Atomic(bool) done;
} Producer;

static int produce(void* user)
{
    Producer* p = user;
    f32       block[BLOCK_FRAMES * 2];

    for (u32 b = 0; b < 100; b++)
    {
        for (u32 i = 0; i < BLOCK_FRAMES; i++)
        {
            f32 s = (f32)i / (f32)BLOCK_FRAMES;
            block[i * 2 + 0] = s;
            block[i * 2 + 1] = -s;
        }

        u32 offset = 0;
        while (offset < BLOCK_FRAMES)
        {
            u32 accepted = mel_pcm_ring_write(p->ring, block + offset * 2, BLOCK_FRAMES - offset);
            offset += accepted;
            if (accepted == 0)
                mel_thread_yield();
        }
    }

    atomic_store(&p->done, true);
    return 0;
}

int main(void)
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    Mel_Pcm_Ring* ring = mel_pcm_ring_create(alloc, 2, 4096);

    Producer   p = { .ring = ring };
    Mel_Thread t;
    mel_thread_spawn(&t, produce, &p, .name = "pcm-producer");

    f32  interleaved[READ_FRAMES * 2];
    f32  left[READ_FRAMES];
    f32  right[READ_FRAMES];
    f32* planar[2] = { left, right };
    f32  left_48k[READ_FRAMES * 2];
    f64  cursor = 0.0;

    const f64 ratio = 44100.0 / 48000.0;

    while (!atomic_load(&p.done) || mel_pcm_ring_read_available(ring) > 0)
    {
        u32 got = mel_pcm_ring_read(ring, interleaved, READ_FRAMES);
        if (got == 0)
        {
            mel_thread_yield();
            continue;
        }

        mel_pcm_deinterleave(planar, interleaved, 2, got);

        u32 want = (u32)((f64)got / ratio);
        u32 out = mel_pcm_resample_linear(left, got, left_48k, want, ratio, &cursor);
        (void)out;
        (void)right;
    }

    mel_thread_join(&t, NULL);
    mel_pcm_ring_destroy(ring);
    return 0;
}
