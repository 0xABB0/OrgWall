#include <stt/stt.h>

#include <audioin/audioin.h>
#include <audiocapture/audiocapture.h>

#include <allocator/allocator.h>
#include <executor/executor.h>
#include <future/future.h>
#include <thread/thread.h>

#include <stdatomic.h>
#include <stdio.h>

#define RATE 16000u

static _Atomic(bool) g_done;

static void on_result(Mel_Stt_Session s, const Mel_Stt_Result* result, void* user)
{
    (void)s;
    (void)user;
    printf("%s%.*s\n", result->final ? "final: " : "... ", (int)result->text.len, result->text.data);
}

static void on_complete(Mel_Stt_Session s, Mel_Stt_Status status, void* user)
{
    (void)s;
    (void)user;
    if (mel_stt_failed(status))
        printf("listen failed: 0x%x\n", status);
    atomic_store(&g_done, true);
}

int main(void)
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    mel_audioin_init(alloc, mel_executor_inline());
    mel_stt_init(alloc);

    Mel_Future* auth = mel_stt_authorize(alloc);
    while (!mel_future_resolved(auth))
        mel_thread_sleep(10 * 1000 * 1000);
    bool granted = mel_stt_auth_is_granted(mel_stt_future_auth(auth));
    mel_stt_future_free(auth);
    if (!granted)
        return 1;

    u32                 nrec = mel_stt_recognizer_count();
    Mel_Stt_Recognizer* recs = mel_alloc(alloc, nrec * sizeof(*recs));
    nrec = mel_stt_recognizer_list(recs, nrec);

    Mel_Stt_Recognizer fed = MEL_STT_RECOGNIZER_NULL;
    for (u32 i = 0; i < nrec; i++)
    {
        Mel_Stt_Recognizer_Describe_Result d = mel_stt_recognizer_describe(recs[i]);
        if (!mel_stt_failed(d.status) && d.value.caps.feed && d.value.caps.vocabulary)
        {
            fed = recs[i];
            break;
        }
    }

    if (mel_stt_recognizer_alive(fed))
    {
        Mel_AudioCapture_Open_Result cap = mel_audiocapture_open(alloc, mel_audioin_default(),
                                                                 (Mel_AudioCapture_Opt){
                                                                     .sample_rate = RATE,
                                                                     .channels = 1,
                                                                     .ring_capacity_frames = RATE,
                                                                 });
        if (!mel_audiocapture_status_failed(cap.status))
        {
            str8 vocab[] = { S8("Melody"), S8("audioin"), S8("wireframe") };

            Mel_Stt_Listen_Result session = mel_stt_listen(fed,
                                                           .feed = true,
                                                           .feed_sample_rate = RATE,
                                                           .partials = true,
                                                           .vocabulary = vocab,
                                                           .vocabulary_count = 3,
                                                           .punctuation = true,
                                                           .on_result = on_result,
                                                           .on_complete = on_complete);
            if (mel_stt_warned(session.status))
                printf("listen lowered: 0x%x\n", session.status);

            f32 buf[1024];
            for (u32 pumped = 0; pumped < RATE * 5;)
            {
                u32 got = mel_audiocapture_read(cap.capture, buf, 1024);
                if (got == 0)
                {
                    mel_thread_sleep(5 * 1000 * 1000);
                    continue;
                }
                mel_stt_feed(session.value, buf, got);
                pumped += got;
            }

            mel_stt_stop(session.value);
            while (!atomic_load(&g_done))
                mel_thread_sleep(10 * 1000 * 1000);

            mel_audiocapture_close(cap.capture);
        }
    }

    mel_dealloc(alloc, recs);
    mel_stt_shutdown();
    mel_audioin_shutdown();
    return 0;
}
