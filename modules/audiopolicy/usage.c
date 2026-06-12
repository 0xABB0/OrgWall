#include <audiopolicy/audiopolicy.h>
#include <audiopolicy/events.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <executor/executor.h>

#include <stdio.h>

typedef struct
{
    bool paused_by_interruption;
} App;

static void on_policy_event(const Mel_AudioPolicy_Event* ev, void* user)
{
    App* app = user;

    if (ev->interruption_began || ev->focus_lost)
    {
        app->paused_by_interruption = true;
        printf("pausing playback\n");
    }
    if ((ev->interruption_ended && ev->should_resume) || ev->focus_gained)
    {
        app->paused_by_interruption = false;
        printf("resuming playback\n");
    }
    if (ev->should_duck)
        printf("ducking volume\n");
    if (ev->route_changed)
        printf("route changed: %s\n", mel_audiopolicy_route_reason_name(ev->reason));
}

int main(void)
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    mel_audiopolicy_init(alloc, mel_executor_inline());

    Mel_AudioPolicy_Status s = mel_audiopolicy_apply((Mel_AudioPolicy){
        .category = &mel_audiopolicy_duplex,
        .mode = &mel_audiopolicy_mode_voice_chat,
        .mix_with_others = true,
        .allow_bluetooth = true,
        .allow_bluetooth_a2dp = true,
    });
    if (mel_audiopolicy_status_warned(s))
        printf("policy lowered: 0x%x\n", s);

    App                 app = { 0 };
    Mel_AudioPolicy_Sub sub = mel_audiopolicy_subscribe(mel_executor_inline(), on_policy_event, &app);

    Mel_AudioPolicy_Status f = mel_audiopolicy_focus_request((Mel_AudioPolicy_Focus_Opt){ .may_duck_me = true });
    (void)f;

    mel_audiopolicy_override_output(&mel_audiopolicy_output_speaker);

    Mel_AudioPolicy in_force = mel_audiopolicy_current();
    printf("category in force: %s\n", mel_audiopolicy_category_name(in_force.category));

    mel_audiopolicy_focus_abandon();
    mel_audiopolicy_unsubscribe(sub);
    mel_audiopolicy_shutdown();
    return 0;
}
