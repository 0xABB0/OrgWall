#include <clipboard/clipboard.h>
#include <clipboard/backend.h>
#include <test/test.h>

#include <allocator/heap.h>
#include <string/str8.h>
#include <string.h>

// This translation unit IS the platform backend for the test: it links its own
// mel_clip__plat_* against the core, an in-memory fake with no system side effects.
static char  fake_text[256];
static usize fake_len;
static u64   fake_seq;
static bool  fake_avail = true;

bool  mel_clip__plat_available(void) { return fake_avail; }
u64   mel_clip__plat_sequence(void) { return fake_seq; }
void* mel_clip__plat_native(void) { return NULL; }

void mel_clip__plat_write(Mel_Clip_Job* j)
{
    fake_len = 0;
    u32 reps = mel_clip_job_rep_count(j, 0);
    for (u32 r = 0; r < reps; r++)
    {
        Mel_Clip_Rep rep = mel_clip_job_rep(j, 0, r);
        if (rep.format == MEL_CLIP_FMT_TEXT)
        {
            usize n = (usize)rep.bytes.len < sizeof fake_text ? (usize)rep.bytes.len : sizeof fake_text - 1;
            memcpy(fake_text, rep.bytes.data, n);
            fake_len = n;
        }
    }
    fake_seq++;
    mel_clip_job_resolve(j, MEL_CLIP_OK);
}

void mel_clip__plat_read(Mel_Clip_Job* j)
{
    if (fake_len)
        mel_clip_job_emit(j, MEL_CLIP_FMT_TEXT, fake_text, fake_len);
    mel_clip_job_resolve(j, fake_len ? MEL_CLIP_OK : (MEL_CLIP_RESULT_EMPTY));
}

void mel_clip__plat_clear(Mel_Clip_Job* j)
{
    fake_len = 0;
    fake_seq++;
    mel_clip_job_resolve(j, MEL_CLIP_OK);
}

void mel_clip__plat_query(Mel_Clip_Job* j)
{
    if (fake_len)
        mel_clip_job_emit_format(j, MEL_CLIP_FMT_TEXT);
    mel_clip_job_resolve(j, MEL_CLIP_OK);
}

static void install_fake(void)
{
    fake_avail = true;
    fake_len = 0;
    fake_seq = 0;
    mel_clip_init(mel_alloc_heap(), NULL);
}

MEL_TEST(clipboard, well_known_formats_have_canonical_mime)
{
    install_fake();
    MEL_EXPECT_EQ_STR8(mel_clip_format_mime(MEL_CLIP_FMT_TEXT), S8("text/plain;charset=utf-8"));
    MEL_EXPECT_EQ_STR8(mel_clip_format_mime(MEL_CLIP_FMT_HTML), S8("text/html"));
    MEL_EXPECT_EQ_STR8(mel_clip_format_mime(MEL_CLIP_FMT_PNG), S8("image/png"));
    mel_clip_shutdown();
}

MEL_TEST(clipboard, register_dedups_and_is_stable)
{
    install_fake();
    Mel_Clip_Format a = mel_clip_format_register(S8("application/x-mel-test"));
    Mel_Clip_Format b = mel_clip_format_register(S8("application/x-mel-test"));
    MEL_EXPECT_EQ(a, b);
    MEL_EXPECT_NEQ(a, MEL_CLIP_FMT_NONE);
    MEL_EXPECT_EQ_STR8(mel_clip_format_mime(a), S8("application/x-mel-test"));
    mel_clip_shutdown();
}

static str8            read_result;
static Mel_Clip_Status read_status;
static char            read_buf[256];
static bool            write_done;
static Mel_Clip_Status write_status;

static void on_text(str8 text, Mel_Clip_Status s, void* user)
{
    (void)user;
    read_status = s;
    usize n = (usize)text.len < sizeof read_buf ? (usize)text.len : sizeof read_buf - 1;
    if (n && text.data)
        memcpy(read_buf, text.data, n);
    read_result = (str8){ (u8*)read_buf, (size)n };
}

static void on_write(Mel_Clip_Status s, void* user)
{
    (void)user;
    write_done = true;
    write_status = s;
}

MEL_TEST(clipboard, write_then_read_text_roundtrips)
{
    install_fake();
    write_done = false;
    write_status = MEL_CLIP_ERROR;
    mel_clip_write_text(S8("hello melody"), on_write);
    MEL_EXPECT(write_done);
    MEL_EXPECT_EQ(write_status & MEL_CLIP_SEVERITY_MASK, (Mel_Clip_Status)MEL_CLIP_OK);

    read_result = STR8_EMPTY;
    read_status = MEL_CLIP_ERROR;
    mel_clip_read_text(on_text);
    MEL_EXPECT_EQ(read_status & MEL_CLIP_SEVERITY_MASK, (Mel_Clip_Status)MEL_CLIP_OK);
    MEL_EXPECT_EQ_STR8(read_result, S8("hello melody"));
    mel_clip_shutdown();
}

MEL_TEST(clipboard, sequence_advances_on_write)
{
    install_fake();
    u64 before = mel_clip_sequence();
    mel_clip_write_text(S8("x"), NULL);
    MEL_EXPECT_GT(mel_clip_sequence(), before);
    mel_clip_shutdown();
}

MEL_TEST(clipboard, empty_clipboard_reports_empty)
{
    install_fake();
    read_status = MEL_CLIP_OK;
    mel_clip_read_text(on_text);
    MEL_EXPECT((read_status & MEL_CLIP_RESULT_EMPTY) != 0);
    mel_clip_shutdown();
}

MEL_TEST(clipboard, no_backend_reports_no_clipboard)
{
    fake_avail = false;
    mel_clip_init(mel_alloc_heap(), NULL);
    MEL_EXPECT(!mel_clip_available());
    write_done = false;
    write_status = MEL_CLIP_OK;
    mel_clip_write_text(S8("nope"), on_write);
    MEL_EXPECT(write_done);
    MEL_EXPECT(mel_clip_failed(write_status));
    MEL_EXPECT((write_status & MEL_CLIP_RESULT_NO_CLIPBOARD) != 0);
    mel_clip_shutdown();
    fake_avail = true;
}

MEL_TEST(clipboard, transferable_build_and_free)
{
    install_fake();
    Mel_Clip_Transferable t;
    mel_clip_transferable_init(&t, mel_alloc_heap());
    Mel_Clip_Item* it = mel_clip_item_add(&t);
    mel_clip_rep_add(it, MEL_CLIP_FMT_TEXT, S8("a"), t.alloc);
    mel_clip_rep_add(it, MEL_CLIP_FMT_HTML, S8("<b>a</b>"), t.alloc);
    MEL_EXPECT_EQ(t.items.count, (usize)1);
    MEL_EXPECT_EQ(t.items.items[0].reps.count, (usize)2);
    mel_clip_transferable_free(&t);
    mel_clip_shutdown();
}
