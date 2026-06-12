#include <audiocapture/audiocapture.h>
#include <audioin/audioin.h>
#include <audioin/permission.h>

#include <allocator/allocator.h>
#include <executor/executor.h>
#include <future/future.h>
#include <thread/thread.h>

#include <stdio.h>

#define RATE   48000u
#define WINDOW 2048u

int main(void)
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    mel_audioin_init(alloc, mel_executor_inline());

    Mel_Future* auth = mel_audioin_authorize(alloc);
    while (!mel_future_resolved(auth))
        mel_thread_sleep(10 * 1000 * 1000);
    bool granted = mel_audioin_auth_is_granted(mel_audioin_future_auth(auth));
    mel_audioin_future_free(auth);
    if (!granted)
    {
        mel_audioin_shutdown();
        return 1;
    }

    Mel_AudioCapture_Open_Result r = mel_audiocapture_open(alloc, mel_audioin_default(),
                                                           (Mel_AudioCapture_Opt){
                                                               .sample_rate = RATE,
                                                               .channels = 1,
                                                               .ring_capacity_frames = RATE / 4,
                                                               .processing = {
                                                                   .echo_cancellation = true,
                                                                   .noise_suppression = true,
                                                               },
                                                           });
    if (mel_audiocapture_status_failed(r.status))
    {
        mel_audioin_shutdown();
        return 1;
    }

    Mel_AudioCapture_Granted g = mel_audiocapture_granted(r.capture);
    if (!g.processing.echo_cancellation)
        printf("no OS echo cancellation on this route\n");

    f32 window[WINDOW];
    u32 fill = 0;
    u64 window_start_ns = 0;

    for (;;)
    {
        Mel_AudioCapture_Status live = mel_audiocapture_status(r.capture);
        if (live & MEL_AUDIOCAPTURE_RESULT_LOST)
        {
            printf("input lost\n");
            break;
        }
        if (live & MEL_AUDIOCAPTURE_WARN_OVERRUN)
            printf("dropped %llu frames so far\n", (unsigned long long)mel_audiocapture_dropped_frames(r.capture));

        Mel_AudioCapture_Read got = mel_audiocapture_read_ex(r.capture, window + fill, WINDOW - fill);
        if (fill == 0 && got.frames > 0)
            window_start_ns = got.timestamp_ns;
        fill += got.frames;

        if (fill < WINDOW)
        {
            mel_thread_sleep(5 * 1000 * 1000);
            continue;
        }

        printf("window captured at %llu ns\n", (unsigned long long)window_start_ns);
        fill = 0;
    }

    mel_audiocapture_close(r.capture);
    mel_audioin_shutdown();
    return 0;
}
