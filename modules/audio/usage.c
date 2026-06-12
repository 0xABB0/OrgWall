#include <audio/audio.h>
#include <audioout/audioout.h>
#include <audioin/audioin.h>
#include <audiocapture/audiocapture.h>
#include <spectrum/spectrum.h>

#include <allocator/allocator.h>
#include <executor/executor.h>
#include <thread/thread.h>

#include <stdio.h>

#define WINDOW 1024u

int main(void)
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    mel_audioout_init(alloc, mel_executor_inline());
    mel_audioin_init(alloc, mel_executor_inline());

    Mel_AudioOut remembered = mel_audioout_find(S8("coreaudio:BuiltInSpeakerDevice"));
    Mel_AudioOut device = mel_audioout_alive(remembered) ? remembered : MEL_AUDIOOUT_NULL;

    Mel_Audio* eng = mel_audio_create(alloc, (Mel_Audio_Opt){
                                                 .samplerate = 48000,
                                                 .channels = 2,
                                                 .block_frames = 256,
                                                 .ring_blocks = 4,
                                                 .master_volume = 1.0f,
                                                 .exec = mel_executor_inline(),
                                                 .device = device,
                                             });

    Mel_AudioCapture_Open_Result cap = mel_audiocapture_open(alloc, mel_audioin_default(),
                                                             (Mel_AudioCapture_Opt){
                                                                 .sample_rate = 48000,
                                                                 .channels = 1,
                                                                 .ring_capacity_frames = 12000,
                                                             });

    Mel_Audio_Voice monitor = { 0 };
    if (!mel_audiocapture_status_failed(cap.status))
    {
        Mel_Audio_Source* mic = mel_audio_pull_source(alloc, (Mel_Audio_Pull_Fn)mel_audiocapture_read,
                                                      cap.capture, 1, 48000);
        monitor = mel_audio_play(eng, mic);
        mel_audio_set_volume(eng, monitor, 0.8f);
    }

    Mel_Audio_Tap* tap = mel_audio_tap_open(eng, alloc, 48000 / 4);
    Mel_Spectrum*  sp = mel_spectrum_create(alloc, WINDOW);
    u32            bins = mel_spectrum_bins(sp);
    f32*           mags = mel_alloc(alloc, bins * sizeof(f32));

    f32 frames[WINDOW * 2];
    f32 mono[WINDOW];
    f32 windowed[WINDOW];

    for (u32 iterations = 0; iterations < 100; iterations++)
    {
        if (mel_audio_tap_available(tap) < WINDOW)
        {
            mel_thread_sleep(5 * 1000 * 1000);
            continue;
        }

        u32 got = mel_audio_tap_read(tap, frames, WINDOW);
        for (u32 i = 0; i < got; i++)
            mono[i] = (frames[i * 2] + frames[i * 2 + 1]) * 0.5f;

        mel_spectrum_hann(windowed, mono, got);
        mel_spectrum_analyze(sp, windowed, mags);

        u32 peak = 0;
        for (u32 b = 1; b < bins; b++)
            if (mags[b] > mags[peak])
                peak = b;
        printf("mix peak: %.1f Hz\n", mel_spectrum_bin_hz(peak, WINDOW, 48000));
    }

    if (mel_audio_voice_valid(eng, monitor))
        mel_audio_stop(eng, monitor);

    Mel_Audio_Status moved = mel_audio_set_device(eng, MEL_AUDIOOUT_NULL);
    if (mel_audio_warned(moved))
        printf("device switch renegotiated the format\n");

    mel_dealloc(alloc, mags);
    mel_spectrum_destroy(sp);
    mel_audio_tap_close(tap);
    mel_audio_destroy(eng);
    if (cap.capture)
        mel_audiocapture_close(cap.capture);
    mel_audioin_shutdown();
    mel_audioout_shutdown();
    return 0;
}
