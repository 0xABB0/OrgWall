#include <test/test.h>

#include <audiopolicy/audiopolicy.h>
#include <audiopolicy/events.h>

#include "../src/audiopolicy_internal.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <core/types.h>
#include <executor/executor.h>

#include <string.h>

typedef struct
{
    u32                             apply_calls;
    u32                             lowering_bits;
    const mel_audiopolicy_category* force_category;
    const mel_audiopolicy_output*   last_override;
    Mel_AudioPolicy_Status          focus_status;
    u32                             focus_requests;
    u32                             focus_abandons;
    bool                            last_may_duck_me;
} Mock_Backend;

static Mock_Backend mockb;

static Mel_AudioPolicy_Status mockb_apply(const Mel_AudioPolicy* requested, Mel_AudioPolicy* in_force)
{
    mockb.apply_calls++;
    *in_force = *requested;
    if (mockb.force_category)
        in_force->category = mockb.force_category;
    if (mockb.lowering_bits & MEL_AUDIOPOLICY_WARN_DUCK_IGNORED)
        in_force->duck_others = false;
    return mockb.lowering_bits;
}

static Mel_AudioPolicy_Status mockb_override(const mel_audiopolicy_output* port)
{
    mockb.last_override = port;
    return MEL_AUDIOPOLICY_OK;
}

static Mel_AudioPolicy_Status mockb_focus_request(Mel_AudioPolicy_Focus_Opt opt)
{
    mockb.focus_requests++;
    mockb.last_may_duck_me = opt.may_duck_me;
    return mockb.focus_status;
}

static void mockb_focus_abandon(void) { mockb.focus_abandons++; }

static const Mel_AudioPolicy_Backend MOCK_BACKEND = {
    .apply = mockb_apply,
    .override_output = mockb_override,
    .focus_request = mockb_focus_request,
    .focus_abandon = mockb_focus_abandon,
};

const Mel_AudioPolicy_Backend* mel_audiopolicy__backend(void) { return &MOCK_BACKEND; }

static void install(void)
{
    memset(&mockb, 0, sizeof mockb);
    mockb.focus_status = MEL_AUDIOPOLICY_OK;
    mel_audiopolicy_init(mel_alloc_heap(), NULL);
}

MEL_TEST(audiopolicy, apply_ok_and_readback)
{
    install();

    Mel_AudioPolicy_Status st = mel_audiopolicy_apply((Mel_AudioPolicy){
        .category = &mel_audiopolicy_duplex,
        .mode = &mel_audiopolicy_mode_voice_chat,
        .mix_with_others = true,
    });
    MEL_EXPECT_EQ(st, (Mel_AudioPolicy_Status)MEL_AUDIOPOLICY_OK);
    MEL_EXPECT_EQ(mockb.apply_calls, 1u);

    Mel_AudioPolicy cur = mel_audiopolicy_current();
    MEL_EXPECT(cur.category == &mel_audiopolicy_duplex);
    MEL_EXPECT(cur.mode == &mel_audiopolicy_mode_voice_chat);
    MEL_EXPECT(cur.mix_with_others);

    mel_audiopolicy_shutdown();
}

MEL_TEST(audiopolicy, apply_normalizes_null_mode)
{
    install();
    mel_audiopolicy_apply((Mel_AudioPolicy){ .category = &mel_audiopolicy_playback });
    MEL_EXPECT(mel_audiopolicy_current().mode == &mel_audiopolicy_mode_default);
    mel_audiopolicy_shutdown();
}

MEL_TEST(audiopolicy, apply_lowering_is_warned_and_honest)
{
    install();
    mockb.lowering_bits = MEL_AUDIOPOLICY_WARN_DUCK_IGNORED | MEL_AUDIOPOLICY_WARN_BLUETOOTH_IGNORED;
    mockb.force_category = &mel_audiopolicy_playback;

    Mel_AudioPolicy_Status st = mel_audiopolicy_apply((Mel_AudioPolicy){
        .category = &mel_audiopolicy_duplex,
        .duck_others = true,
        .allow_bluetooth = true,
    });
    MEL_EXPECT(mel_audiopolicy_status_warned(st));
    MEL_EXPECT(st & MEL_AUDIOPOLICY_WARN_DUCK_IGNORED);
    MEL_EXPECT(st & MEL_AUDIOPOLICY_WARN_BLUETOOTH_IGNORED);

    Mel_AudioPolicy cur = mel_audiopolicy_current();
    MEL_EXPECT(cur.category == &mel_audiopolicy_playback);
    MEL_EXPECT(!cur.duck_others);

    mel_audiopolicy_shutdown();
}

MEL_TEST(audiopolicy, current_before_apply_is_untouched)
{
    install();
    Mel_AudioPolicy cur = mel_audiopolicy_current();
    MEL_EXPECT_NULL(cur.category);
    MEL_EXPECT_NULL(cur.mode);
    mel_audiopolicy_shutdown();
}

MEL_TEST(audiopolicy, override_round_trip)
{
    install();
    Mel_AudioPolicy_Status st = mel_audiopolicy_override_output(&mel_audiopolicy_output_speaker);
    MEL_EXPECT(!mel_audiopolicy_status_failed(st));
    MEL_EXPECT(mockb.last_override == &mel_audiopolicy_output_speaker);
    mel_audiopolicy_shutdown();
}

typedef struct
{
    u32                                 interruptions_began;
    u32                                 interruptions_ended;
    u32                                 should_resume;
    u32                                 should_duck;
    u32                                 focus_lost;
    u32                                 focus_gained;
    u32                                 route_changed;
    const mel_audiopolicy_route_reason* last_reason;
} Tally;

static void on_event(const Mel_AudioPolicy_Event* ev, void* user)
{
    Tally* t = user;
    if (ev->interruption_began)
        t->interruptions_began++;
    if (ev->interruption_ended)
        t->interruptions_ended++;
    if (ev->should_resume)
        t->should_resume++;
    if (ev->should_duck)
        t->should_duck++;
    if (ev->focus_lost)
        t->focus_lost++;
    if (ev->focus_gained)
        t->focus_gained++;
    if (ev->route_changed)
    {
        t->route_changed++;
        t->last_reason = ev->reason;
    }
}

MEL_TEST(audiopolicy, focus_sequencing_with_events)
{
    install();
    Tally               tally = { 0 };
    Mel_AudioPolicy_Sub sub = mel_audiopolicy_subscribe(NULL, on_event, &tally);

    Mel_AudioPolicy_Status st = mel_audiopolicy_focus_request((Mel_AudioPolicy_Focus_Opt){ .may_duck_me = true });
    MEL_EXPECT(!mel_audiopolicy_status_failed(st));
    MEL_EXPECT_EQ(mockb.focus_requests, 1u);
    MEL_EXPECT(mockb.last_may_duck_me);

    mel_audiopolicy__emit(&(Mel_AudioPolicy_Event){ .focus_lost = true });
    mel_audiopolicy__emit(&(Mel_AudioPolicy_Event){ .should_duck = true });
    mel_audiopolicy__emit(&(Mel_AudioPolicy_Event){ .focus_gained = true });
    MEL_EXPECT_EQ(tally.focus_lost, 1u);
    MEL_EXPECT_EQ(tally.should_duck, 1u);
    MEL_EXPECT_EQ(tally.focus_gained, 1u);

    mel_audiopolicy_focus_abandon();
    MEL_EXPECT_EQ(mockb.focus_abandons, 1u);

    mel_audiopolicy_unsubscribe(sub);
    mel_audiopolicy_shutdown();
}

MEL_TEST(audiopolicy, interruption_event_payloads)
{
    install();
    Tally               tally = { 0 };
    Mel_AudioPolicy_Sub sub = mel_audiopolicy_subscribe(NULL, on_event, &tally);

    mel_audiopolicy__emit(&(Mel_AudioPolicy_Event){ .interruption_began = true });
    MEL_EXPECT_EQ(tally.interruptions_began, 1u);

    mel_audiopolicy__emit(&(Mel_AudioPolicy_Event){ .interruption_ended = true, .should_resume = true });
    MEL_EXPECT_EQ(tally.interruptions_ended, 1u);
    MEL_EXPECT_EQ(tally.should_resume, 1u);

    mel_audiopolicy__emit(&(Mel_AudioPolicy_Event){ .route_changed = true, .reason = &mel_audiopolicy_route_device_removed });
    MEL_EXPECT_EQ(tally.route_changed, 1u);
    MEL_EXPECT(tally.last_reason == &mel_audiopolicy_route_device_removed);

    mel_audiopolicy_unsubscribe(sub);
    mel_audiopolicy_shutdown();
}

MEL_TEST(audiopolicy, unsubscribe_stops_delivery)
{
    install();
    Tally               tally = { 0 };
    Mel_AudioPolicy_Sub sub = mel_audiopolicy_subscribe(NULL, on_event, &tally);

    mel_audiopolicy__emit(&(Mel_AudioPolicy_Event){ .should_duck = true });
    MEL_EXPECT_EQ(tally.should_duck, 1u);

    mel_audiopolicy_unsubscribe(sub);
    mel_audiopolicy__emit(&(Mel_AudioPolicy_Event){ .should_duck = true });
    MEL_EXPECT_EQ(tally.should_duck, 1u);

    mel_audiopolicy_shutdown();
}

MEL_TEST(audiopolicy, focus_request_failure_does_not_hold)
{
    install();
    mockb.focus_status = MEL_AUDIOPOLICY_ERROR | MEL_AUDIOPOLICY_RESULT_BUSY;

    Mel_AudioPolicy_Status st = mel_audiopolicy_focus_request((Mel_AudioPolicy_Focus_Opt){ 0 });
    MEL_EXPECT(mel_audiopolicy_status_failed(st));
    MEL_EXPECT_EQ(mockb.focus_requests, 1u);

    mockb.focus_status = MEL_AUDIOPOLICY_OK;
    st = mel_audiopolicy_focus_request((Mel_AudioPolicy_Focus_Opt){ 0 });
    MEL_EXPECT(!mel_audiopolicy_status_failed(st));
    mel_audiopolicy_focus_abandon();
    MEL_EXPECT_EQ(mockb.focus_abandons, 1u);

    mel_audiopolicy_shutdown();
}
