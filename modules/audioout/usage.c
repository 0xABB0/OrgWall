#include <audioout/audioout.h>

#include <allocator/allocator.h>
#include <executor/executor.h>

#include <stdio.h>

static void on_output_event(const Mel_AudioOut_Event* ev, void* user)
{
    (void)user;
    if (ev->default_changed)
        printf("default output changed\n");
    if (ev->removed)
        printf("output removed\n");
    if (ev->changed)
        printf("output changed (volume/route)\n");
}

int main(void)
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    mel_audioout_init(alloc, mel_executor_inline());

    Mel_AudioOut_Hotplug_Sub sub = mel_audioout_subscribe(mel_executor_inline(), on_output_event, NULL);

    u32           count = mel_audioout_count();
    Mel_AudioOut* outs = mel_alloc(alloc, count * sizeof(*outs));
    count = mel_audioout_list(outs, count);

    Mel_AudioOut def = mel_audioout_default();

    for (u32 i = 0; i < count; i++)
    {
        Mel_AudioOut_Describe_Result d = mel_audioout_describe(outs[i], alloc);
        if (mel_audioout_status_failed(d.status))
            continue;

        printf("%.*s [%s] %u ch @ %u Hz  id=%.*s%s\n",
               (int)d.value.name.len, d.value.name.data,
               mel_audioout_kind_name(d.value.kind),
               d.value.channels, d.value.samplerate,
               (int)d.value.stable_id.len, d.value.stable_id.data,
               mel_audioout_equal(outs[i], def) ? " (default)" : "");

        if (d.value.caps.volume && mel_audioout_muted(outs[i]))
            mel_audioout_set_muted(outs[i], false);

        mel_audioout_describe_free(&d);
    }

    Mel_AudioOut_Publish_Result pub = mel_audioout_publish(alloc, (Mel_AudioOut_Publish_Opt){
                                                                      .name = S8("My App Sink"),
                                                                      .channels = 2,
                                                                      .samplerate = 48000,
                                                                      .ring_capacity_frames = 24000,
                                                                  });
    if (!mel_audioout_status_failed(pub.status))
    {
        printf("published output, os visible: %d\n", mel_audioout_publish_os_visible(pub.published));

        f32 incoming[960];
        u32 got = mel_audioout_publish_read(pub.published, incoming, 480);
        (void)got;

        mel_audioout_unpublish(pub.published);
    }

    mel_dealloc(alloc, outs);
    mel_audioout_unsubscribe(sub);
    mel_audioout_shutdown();
    return 0;
}
