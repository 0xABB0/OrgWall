#include <messagebox/messagebox.h>
#include <messagebox/backend.h>
#include <test/test.h>

#include <string/str8.h>

#include <string.h>

static bool                fake_available = true;
static Mel_Msgbox_Request  fake_last;
static bool                fake_saw_request;
static i32                 fake_return_id;
static Mel_Msgbox_Status   fake_return_status;

static void fake_reset(void)
{
    fake_available = true;
    fake_saw_request = false;
    fake_return_id = 0;
    fake_return_status = MEL_MSGBOX_OK;
    memset(&fake_last, 0, sizeof fake_last);
}

bool mel_msgbox__plat_available(void) { return fake_available; }

Mel_Msgbox_Status mel_msgbox__plat_show(const Mel_Msgbox_Request* req, i32* out_chosen_id)
{
    fake_last = *req;
    fake_saw_request = true;
    *out_chosen_id = fake_return_id;
    return fake_return_status;
}

MEL_TEST(messagebox, alert_is_single_ok_button)
{
    fake_reset();
    fake_return_id = 0;
    Mel_Msgbox_Status s = mel_msgbox_alert(S8("Heads up"), S8("Disk almost full"));
    MEL_EXPECT(fake_saw_request);
    MEL_EXPECT_EQ(fake_last.button_count, (u32)1);
    MEL_EXPECT_EQ(s & MEL_MSGBOX_SEVERITY_MASK, (Mel_Msgbox_Status)MEL_MSGBOX_OK);
}

MEL_TEST(messagebox, alert_carries_title_and_message)
{
    fake_reset();
    mel_msgbox_alert(S8("Title"), S8("Body"));
    MEL_EXPECT_EQ_STR8(fake_last.title, S8("Title"));
    MEL_EXPECT_EQ_STR8(fake_last.message, S8("Body"));
}

MEL_TEST(messagebox, alert_default_severity_is_info)
{
    fake_reset();
    mel_msgbox_alert(S8("t"), S8("m"));
    MEL_EXPECT_EQ(fake_last.severity, (Mel_Msgbox_Severity)MEL_MSGBOX_SEVERITY_INFO);
}

MEL_TEST(messagebox, show_returns_chosen_button_id)
{
    fake_reset();
    Mel_Msgbox_Button bs[] = { { S8("Save"), 10 }, { S8("Discard"), 20 }, { S8("Cancel"), 30 } };
    fake_return_id = 20;
    Mel_Msgbox_Result r = mel_msgbox_show(.title = S8("Quit?"), .message = S8("Unsaved changes"), .buttons = bs, .button_count = 3);
    MEL_EXPECT_EQ(r.chosen_id, (i32)20);
    MEL_EXPECT_EQ(fake_last.button_count, (u32)3);
    MEL_EXPECT(!mel_msgbox_failed(r.status));
}

MEL_TEST(messagebox, default_id_falls_back_to_first_button)
{
    fake_reset();
    Mel_Msgbox_Button bs[] = { { S8("A"), 1 }, { S8("B"), 2 } };
    mel_msgbox_show(.buttons = bs, .button_count = 2);
    MEL_EXPECT_EQ(fake_last.default_id, (i32)1);
}

MEL_TEST(messagebox, escape_id_falls_back_to_last_button)
{
    fake_reset();
    Mel_Msgbox_Button bs[] = { { S8("A"), 1 }, { S8("B"), 2 }, { S8("C"), 3 } };
    mel_msgbox_show(.buttons = bs, .button_count = 3);
    MEL_EXPECT_EQ(fake_last.escape_id, (i32)3);
}

MEL_TEST(messagebox, explicit_default_and_escape_are_honored)
{
    fake_reset();
    Mel_Msgbox_Button bs[] = { { S8("A"), 1 }, { S8("B"), 2 }, { S8("C"), 3 } };
    mel_msgbox_show(.buttons = bs, .button_count = 3, .has_default_id = true, .default_id = 2, .has_escape_id = true, .escape_id = 1);
    MEL_EXPECT_EQ(fake_last.default_id, (i32)2);
    MEL_EXPECT_EQ(fake_last.escape_id, (i32)1);
}

MEL_TEST(messagebox, severity_passes_through)
{
    fake_reset();
    Mel_Msgbox_Button bs[] = { { S8("OK"), 0 } };
    mel_msgbox_show(.buttons = bs, .button_count = 1, .severity = MEL_MSGBOX_SEVERITY_ERROR);
    MEL_EXPECT_EQ(fake_last.severity, (Mel_Msgbox_Severity)MEL_MSGBOX_SEVERITY_ERROR);
}

MEL_TEST(messagebox, no_backend_reports_no_backend_and_picks_escape)
{
    fake_reset();
    fake_available = false;
    Mel_Msgbox_Button bs[] = { { S8("Retry"), 1 }, { S8("Abort"), 2 } };
    Mel_Msgbox_Result r = mel_msgbox_show(.buttons = bs, .button_count = 2);
    MEL_EXPECT(mel_msgbox_failed(r.status));
    MEL_EXPECT((r.status & MEL_MSGBOX_RESULT_NO_BACKEND) != 0);
    MEL_EXPECT_EQ(r.chosen_id, (i32)2);
    MEL_EXPECT(!fake_saw_request);
}

MEL_TEST(messagebox, no_backend_alert_does_not_crash)
{
    fake_reset();
    fake_available = false;
    Mel_Msgbox_Status s = mel_msgbox_alert(S8("boom"), S8("startup failed"));
    MEL_EXPECT(mel_msgbox_failed(s));
    MEL_EXPECT((s & MEL_MSGBOX_RESULT_NO_BACKEND) != 0);
}

MEL_TEST(messagebox, available_reflects_backend)
{
    fake_reset();
    MEL_EXPECT(mel_msgbox_available());
    fake_available = false;
    MEL_EXPECT(!mel_msgbox_available());
}

MEL_TEST(messagebox, empty_title_synthesizes_warning)
{
    fake_reset();
    Mel_Msgbox_Button bs[] = { { S8("OK"), 0 } };
    Mel_Msgbox_Result r = mel_msgbox_show(.message = S8("no title"), .buttons = bs, .button_count = 1);
    MEL_EXPECT((r.status & MEL_MSGBOX_WARN_TITLE_SYNTHESIZED) != 0);
    MEL_EXPECT(mel_msgbox_warned(r.status));
}

MEL_TEST(messagebox, color_scheme_passed_to_backend)
{
    fake_reset();
    Mel_Msgbox_Button bs[] = { { S8("OK"), 0 } };
    Mel_Msgbox_Color  accent = { .has_value = true, .value = { 0x11, 0x22, 0x33, 0xff } };
    mel_msgbox_show(.title = S8("t"), .buttons = bs, .button_count = 1, .accent = accent);
    MEL_EXPECT(fake_last.accent.has_value);
    MEL_EXPECT_EQ(fake_last.accent.value.r, (u8)0x11);
    MEL_EXPECT_EQ(fake_last.accent.value.g, (u8)0x22);
    MEL_EXPECT_EQ(fake_last.accent.value.b, (u8)0x33);
}

MEL_TEST(messagebox, rtl_flag_passed_to_backend)
{
    fake_reset();
    Mel_Msgbox_Button bs[] = { { S8("OK"), 0 } };
    mel_msgbox_show(.title = S8("t"), .buttons = bs, .button_count = 1, .right_to_left = true);
    MEL_EXPECT(fake_last.right_to_left);
}

MEL_TEST(messagebox, none_parent_yields_null_native)
{
    fake_reset();
    Mel_Msgbox_Button bs[] = { { S8("OK"), 0 } };
    mel_msgbox_show(.title = S8("t"), .buttons = bs, .button_count = 1);
    MEL_EXPECT_NULL(fake_last.native_parent);
}

MEL_TEST(messagebox, show_with_no_buttons_uses_implicit_ok)
{
    fake_reset();
    Mel_Msgbox_Result r = mel_msgbox_show(.title = S8("t"), .message = S8("m"));
    MEL_EXPECT(fake_saw_request);
    MEL_EXPECT_EQ(fake_last.button_count, (u32)1);
    MEL_EXPECT(!mel_msgbox_failed(r.status));
}

MEL_TEST(messagebox, backend_status_merges_with_core_warnings)
{
    fake_reset();
    fake_return_status = MEL_MSGBOX_OK;
    Mel_Msgbox_Button bs[] = { { S8("OK"), 7 } };
    fake_return_id = 7;
    Mel_Msgbox_Result r = mel_msgbox_show(.message = S8("no title here"), .buttons = bs, .button_count = 1);
    MEL_EXPECT_EQ(r.chosen_id, (i32)7);
    MEL_EXPECT(mel_msgbox_warned(r.status));
    MEL_EXPECT((r.status & MEL_MSGBOX_WARN_TITLE_SYNTHESIZED) != 0);
}

MEL_TEST(messagebox, backend_error_dominates_severity)
{
    fake_reset();
    fake_return_status = MEL_MSGBOX_ERROR;
    Mel_Msgbox_Button bs[] = { { S8("OK"), 0 } };
    Mel_Msgbox_Result r = mel_msgbox_show(.title = S8("t"), .buttons = bs, .button_count = 1);
    MEL_EXPECT(mel_msgbox_failed(r.status));
}
