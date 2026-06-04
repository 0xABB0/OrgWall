#include <test/test.h>

#include <debug/debug.h>

#include <string.h>

typedef struct
{
    u32                 calls;
    str8                last_condition;
    str8                last_location;
    str8                last_message;
    u32                 last_level;
    Mel_Assert_Response respond_with;
} Capture;

static Mel_Assert_Response capture_handler(const Mel_Assert_Report* report, void* user)
{
    Capture* cap = (Capture*)user;
    cap->calls++;
    cap->last_condition = report->condition;
    cap->last_location = report->location;
    cap->last_message = report->message;
    cap->last_level = report->level;
    return cap->respond_with;
}

MEL_TEST(debug_assert, level_wired_to_build_config)
{
    MEL_EXPECT_EQ(MEL_ASSERT_LEVEL, MEL_ASSERT_LEVEL_DEBUG);
    MEL_EXPECT_EQ(MEL_ASSERT_ENABLED, 1);
}

MEL_TEST(debug_assert, handler_install_roundtrip)
{
    Capture cap = { 0 };
    mel_assert_install_handler(capture_handler, &cap);
    Mel_Assert_Handler_Slot slot = mel_assert_handler();
    MEL_EXPECT(slot.handler == capture_handler);
    MEL_EXPECT(slot.user == &cap);
    mel_assert_install_handler(NULL, NULL);
}

MEL_TEST(debug_assert, true_condition_does_not_invoke_handler)
{
    Capture cap = { 0 };
    cap.respond_with = MEL_ASSERT_RESPONSE_IGNORE_ONCE;
    mel_assert_install_handler(capture_handler, &cap);

    mel_assert(1 == 1);

    MEL_EXPECT_EQ(cap.calls, 0u);
    mel_assert_install_handler(NULL, NULL);
}

MEL_TEST(debug_assert, failing_assert_reaches_handler_with_fields)
{
    Capture cap = { 0 };
    cap.respond_with = MEL_ASSERT_RESPONSE_IGNORE_ONCE;
    mel_assert_install_handler(capture_handler, &cap);

    int x = 2;
    mel_assert(x == 3);

    MEL_EXPECT_EQ(cap.calls, 1u);
    MEL_EXPECT_EQ(cap.last_level, (u32)MEL_ASSERT_LEVEL_DEBUG);
    MEL_EXPECT(memcmp(cap.last_condition.data, "x == 3", 6) == 0);
    MEL_EXPECT(cap.last_location.len > 0);
    mel_assert_install_handler(NULL, NULL);
}

MEL_TEST(debug_assert, message_variant_carries_text)
{
    Capture cap = { 0 };
    cap.respond_with = MEL_ASSERT_RESPONSE_IGNORE_ONCE;
    mel_assert_install_handler(capture_handler, &cap);

    mel_assert_msg("budget exceeded", 0);

    MEL_EXPECT_EQ(cap.calls, 1u);
    MEL_EXPECT(str8_equals(cap.last_message, S8("budget exceeded")));
    mel_assert_install_handler(NULL, NULL);
}

MEL_TEST(debug_assert, ignore_once_does_not_silence_site)
{
    Capture cap = { 0 };
    cap.respond_with = MEL_ASSERT_RESPONSE_IGNORE_ONCE;
    mel_assert_install_handler(capture_handler, &cap);

    for (int i = 0; i < 3; i++)
        mel_assert(0 && "ignore-once site");

    MEL_EXPECT_EQ(cap.calls, 3u);
    mel_assert_install_handler(NULL, NULL);
}

MEL_TEST(debug_assert, ignore_forever_silences_site)
{
    Capture cap = { 0 };
    cap.respond_with = MEL_ASSERT_RESPONSE_IGNORE_FOREVER;
    mel_assert_install_handler(capture_handler, &cap);

    for (int i = 0; i < 5; i++)
        mel_assert(0 && "ignore-forever site");

    MEL_EXPECT_EQ(cap.calls, 1u);
    mel_assert_install_handler(NULL, NULL);
}

static int     g_retry_counter = 0;
static Capture g_retry_cap;

static Mel_Assert_Response retry_handler(const Mel_Assert_Report* report, void* user)
{
    (void)report;
    (void)user;
    g_retry_cap.calls++;
    return MEL_ASSERT_RESPONSE_RETRY;
}

MEL_TEST(debug_assert, retry_reevaluates_condition)
{
    g_retry_counter = 0;
    g_retry_cap = (Capture){ 0 };
    mel_assert_install_handler(retry_handler, NULL);

    mel_assert((g_retry_counter++) >= 2);

    MEL_EXPECT_EQ(g_retry_cap.calls, 2u);
    MEL_EXPECT_EQ(g_retry_counter, 3);
    mel_assert_install_handler(NULL, NULL);
}

MEL_TEST(debug_assert, paranoid_compiled_out_at_debug_level)
{
    Capture cap = { 0 };
    cap.respond_with = MEL_ASSERT_RESPONSE_IGNORE_ONCE;
    mel_assert_install_handler(capture_handler, &cap);

    mel_assert_paranoid(0 && "should be compiled out");

    MEL_EXPECT_EQ(cap.calls, 0u);
    mel_assert_install_handler(NULL, NULL);
}

MEL_TEST(debug_assert, release_assert_fires_at_debug_level)
{
    Capture cap = { 0 };
    cap.respond_with = MEL_ASSERT_RESPONSE_IGNORE_ONCE;
    mel_assert_install_handler(capture_handler, &cap);

    mel_assert_release(0 && "release-level check");

    MEL_EXPECT_EQ(cap.calls, 1u);
    MEL_EXPECT_EQ(cap.last_level, (u32)MEL_ASSERT_LEVEL_RELEASE);
    mel_assert_install_handler(NULL, NULL);
}

MEL_TEST(debug_assert, response_predicates)
{
    MEL_EXPECT(mel_assert_response_retry(MEL_ASSERT_RESPONSE_RETRY));
    MEL_EXPECT(mel_assert_response_break(MEL_ASSERT_RESPONSE_BREAK));
    MEL_EXPECT(mel_assert_response_abort(MEL_ASSERT_RESPONSE_ABORT));
    MEL_EXPECT(mel_assert_response_ignored(MEL_ASSERT_RESPONSE_IGNORE_ONCE));
    MEL_EXPECT(mel_assert_response_ignored(MEL_ASSERT_RESPONSE_IGNORE_FOREVER));
    MEL_EXPECT(mel_assert_response_ignore_forever(MEL_ASSERT_RESPONSE_IGNORE_FOREVER));
    MEL_EXPECT(!mel_assert_response_ignore_forever(MEL_ASSERT_RESPONSE_IGNORE_ONCE));
    MEL_EXPECT(!mel_assert_response_retry(MEL_ASSERT_RESPONSE_ABORT));
}

MEL_TEST(debug_assert, default_handler_aborts_and_breaks_in_debug)
{
    Mel_Assert_Report report = {
        .condition = S8("x"),
        .location = S8("test.c:1"),
        .message = STR8_EMPTY,
        .level = MEL_ASSERT_LEVEL_DEBUG,
        .stack = NULL,
    };
    Mel_Assert_Response r = mel_assert_default_handler(&report, NULL);
    MEL_EXPECT(mel_assert_response_abort(r));
    MEL_EXPECT(mel_assert_response_break(r));
}

MEL_TEST(debug_assert, dialog_bridge_maps_responses)
{
    Capture cap = { 0 };

    cap.respond_with = MEL_ASSERT_RESPONSE_RETRY;
    mel_assert_install_handler(capture_handler, &cap);
    MEL_EXPECT_EQ(mel_assert_dialog(false, S8("c"), S8("d"), NULL), ASSERT_DIALOG_RESULT_RETRY);

    cap.respond_with = MEL_ASSERT_RESPONSE_IGNORE_ONCE;
    MEL_EXPECT_EQ(mel_assert_dialog(false, S8("c"), S8("d"), NULL), ASSERT_DIALOG_RESULT_IGNORE);

    cap.respond_with = MEL_ASSERT_RESPONSE_ABORT;
    MEL_EXPECT_EQ(mel_assert_dialog(false, S8("c"), S8("d"), NULL), ASSERT_DIALOG_RESULT_ABORT);

    MEL_EXPECT_EQ(mel_assert_dialog(true, S8("c"), S8("d"), NULL), ASSERT_DIALOG_RESULT_IGNORE);

    mel_assert_install_handler(NULL, NULL);
}

MEL_TEST(debug_assert, interactive_handler_symbols_link)
{
    Mel_Assert_Handler h = mel_assert_interactive_handler;
    MEL_EXPECT(h != NULL);
    MEL_EXPECT(mel_assert_interactive_available() == mel_assert_interactive_available());
}
