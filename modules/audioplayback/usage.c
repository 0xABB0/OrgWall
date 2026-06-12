#include <audioplayback/audioplayback.h>
#include <audioout/audioout.h>

#include <allocator/allocator.h>
#include <executor/executor.h>
#include <thread/thread.h>

#include <math.h>
#include <stdio.h>

#define RATE  48000u
#define BLOCK 480u

int main(void)
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    mel_audioout_init(alloc, mel_executor_inline());

    Mel_AudioPlayback_Open_Result r = mel_audioplayback_open(alloc, mel_audioout_default(),
                                                             (Mel_AudioPlayback_Opt){
                                                                 .sample_rate = RATE,
                                                                 .channels = 2,
                                                                 .ring_capacity_frames = RATE / 10,
                                                                 .exclusive = true,
                                                             });
    if (mel_audioplayback_status_failed(r.status))
    {
        mel_audioout_shutdown();
        return 1;
    }

    Mel_AudioPlayback_Granted g = mel_audioplayback_granted(r.playback);
    if (!g.exclusive)
        printf("shared mode; output latency %u frames\n", mel_audioplayback_latency_frames(r.playback));

    f32 block[BLOCK * 2];
    f64 phase = 0.0;

    for (u32 written_total = 0; written_total < RATE * 2;)
    {
        Mel_AudioPlayback_Status live = mel_audioplayback_status(r.playback);
        if (live & MEL_AUDIOPLAYBACK_RESULT_LOST)
        {
            printf("output lost\n");
            break;
        }
        if (live & MEL_AUDIOPLAYBACK_WARN_UNDERRUN)
            printf("starved %llu frames so far\n", (unsigned long long)mel_audioplayback_underrun_frames(r.playback));

        for (u32 i = 0; i < BLOCK; i++)
        {
            f32 s = (f32)sin(phase) * 0.2f;
            block[i * 2 + 0] = s;
            block[i * 2 + 1] = s;
            phase += 2.0 * 3.14159265358979 * 440.0 / RATE;
        }

        u32 offset = 0;
        while (offset < BLOCK)
        {
            u32 accepted = mel_audioplayback_write(r.playback, block + offset * 2, BLOCK - offset);
            offset += accepted;
            if (accepted == 0)
                mel_thread_sleep(2 * 1000 * 1000);
        }
        written_total += BLOCK;
    }

    mel_audioplayback_close(r.playback);
    mel_audioout_shutdown();
    return 0;
}
