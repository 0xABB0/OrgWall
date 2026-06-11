#include <audioin/audioin.h>
#include <audioin/events.h>
#include <audioin/permission.h>
#include <audioin/os.h>

#include <allocator/allocator.h>
#include <executor/executor.h>
#include <future/future.h>
#include <thread/thread.h>

#include <stdio.h>

static void on_input_event(const Mel_AudioIn_Event* ev, void* user)
{
    (void)user;
    if (ev->added)
        printf("input added\n");
    if (ev->removed)
        printf("input removed\n");
    if (ev->default_changed)
        printf("default input changed\n");
}

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

    Mel_AudioIn_Hotplug_Sub sub = mel_audioin_subscribe(mel_executor_inline(), on_input_event, NULL);

    Mel_AudioIn remembered = mel_audioin_find(S8("coreaudio:BuiltInMicrophoneDevice"));
    Mel_AudioIn def = mel_audioin_default();
    Mel_AudioIn chosen = mel_audioin_alive(remembered) ? remembered : def;

    u32          count = mel_audioin_count();
    Mel_AudioIn* devs = mel_alloc(alloc, count * sizeof(*devs));
    count = mel_audioin_list(devs, count);

    for (u32 i = 0; i < count; i++)
    {
        Mel_AudioIn_Describe_Result d = mel_audioin_describe(devs[i], alloc);
        if (mel_audioin_status_failed(d.status))
            continue;

        printf("%.*s [%s] %u ch @ %u Hz  id=%.*s%s%s\n",
               (int)d.value.name.len, d.value.name.data,
               mel_audioin_kind_name(d.value.kind),
               d.value.channels, d.value.samplerate,
               (int)d.value.stable_id.len, d.value.stable_id.data,
               mel_audioin_equal(devs[i], def) ? " (default)" : "",
               mel_audioin_equal(devs[i], chosen) ? " (chosen)" : "");

        if (d.value.caps.gain)
            mel_audioin_set_gain(devs[i], 0.75f);

        mel_audioin_describe_free(&d);
    }

    Mel_AudioIn_Publish_Result pub = mel_audioin_publish(alloc, (Mel_AudioIn_Publish_Opt){
                                                                    .name = S8("My App Feed"),
                                                                    .channels = 1,
                                                                    .samplerate = 48000,
                                                                    .ring_capacity_frames = 12000,
                                                                });
    if (!mel_audioin_status_failed(pub.status))
    {
        printf("published input, os visible: %d\n", mel_audioin_publish_os_visible(pub.published));

        f32 silence[480] = { 0 };
        mel_audioin_publish_feed(pub.published, silence, 480);

        mel_audioin_unpublish(pub.published);
    }

    mel_dealloc(alloc, devs);
    mel_audioin_unsubscribe(sub);
    mel_audioin_shutdown();
    return 0;
}
