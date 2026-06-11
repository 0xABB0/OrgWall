#include <tts/tts.h>

#include <audio/audio.h>
#include <audioout/audioout.h>

#include <allocator/allocator.h>
#include <executor/executor.h>
#include <thread/thread.h>

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    Mel_Audio*       eng;
    const Mel_Alloc* alloc;
    _Atomic(bool)    done;
} App;

static void on_viseme(Mel_Tts_Utterance u, Mel_Tts_Viseme v, void* user)
{
    (void)u;
    (void)user;
    printf("viseme %u at byte %zu\n", v.viseme, v.range.offset);
}

static void on_complete(Mel_Tts_Utterance u, Mel_Tts_Status status, void* user)
{
    (void)u;
    App* app = user;
    if (mel_tts_failed(status))
        printf("speak failed\n");
    atomic_store(&app->done, true);
}

static void on_render(Mel_Tts_Utterance u, const Mel_Tts_Render* pcm, Mel_Tts_Status status, void* user)
{
    (void)u;
    App* app = user;
    if (mel_tts_failed(status) || pcm == NULL)
    {
        atomic_store(&app->done, true);
        return;
    }

    usize bytes = (usize)pcm->frame_count * pcm->channels * sizeof(f32);
    f32*  copy = mel_alloc(app->alloc, bytes);
    memcpy(copy, pcm->frames, bytes);

    Mel_Audio_Source* src = mel_audio_pcm_from_float(app->alloc, copy, pcm->frame_count,
                                                     pcm->channels, pcm->sample_rate,
                                                     MEL_AUDIO_OWNERSHIP_OWNED);
    mel_audio_play(app->eng, src);
    atomic_store(&app->done, true);
}

int main(void)
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    mel_audioout_init(alloc, mel_executor_inline());
    mel_tts_init(alloc);

    Mel_Audio* eng = mel_audio_create(alloc, (Mel_Audio_Opt){
                                                 .samplerate = 48000,
                                                 .channels = 2,
                                                 .block_frames = 256,
                                                 .ring_blocks = 4,
                                                 .master_volume = 1.0f,
                                                 .exec = mel_executor_inline(),
                                                 .device = MEL_AUDIOOUT_NULL,
                                             });

    App app = { .eng = eng, .alloc = alloc };

    u32            nvoice = mel_tts_voice_count();
    Mel_Tts_Voice* voices = mel_alloc(alloc, nvoice * sizeof(*voices));
    nvoice = mel_tts_voice_list(voices, nvoice);

    Mel_Tts_Voice ssml_voice = MEL_TTS_VOICE_NULL;
    Mel_Tts_Voice render_voice = MEL_TTS_VOICE_NULL;
    for (u32 i = 0; i < nvoice; i++)
    {
        Mel_Tts_Voice_Describe_Result d = mel_tts_voice_describe(voices[i]);
        if (mel_tts_failed(d.status))
            continue;
        if (d.value.caps.ssml && d.value.caps.visemes && !mel_tts_voice_alive(ssml_voice))
            ssml_voice = voices[i];
        if (d.value.caps.render && !mel_tts_voice_alive(render_voice))
            render_voice = voices[i];
    }

    if (mel_tts_voice_alive(ssml_voice))
    {
        Mel_Tts_Speak_Result r = mel_tts_speak(ssml_voice,
                                               S8("<speak>hello <emphasis>there</emphasis></speak>"),
                                               .ssml = true,
                                               .rate = 1.1f,
                                               .on_viseme = on_viseme,
                                               .on_complete = on_complete,
                                               .user = &app);
        if (mel_tts_warned(r.status))
            printf("speak lowered: 0x%x\n", r.status);

        while (!atomic_load(&app.done))
            mel_thread_sleep(10 * 1000 * 1000);
        atomic_store(&app.done, false);
    }

    if (mel_tts_voice_alive(render_voice))
    {
        mel_tts_render(render_voice, S8("rendered, not spoken"),
                       .rate = 1.2f,
                       .on_render = on_render,
                       .user = &app);

        while (!atomic_load(&app.done))
            mel_thread_sleep(10 * 1000 * 1000);

        mel_thread_sleep(2ll * 1000 * 1000 * 1000);
    }

    mel_dealloc(alloc, voices);
    mel_audio_destroy(eng);
    mel_tts_shutdown();
    mel_audioout_shutdown();
    return 0;
}
