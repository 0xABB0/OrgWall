#include <platform/platform.h>
#include <platform/hooks.h>
#include <test/test.h>

#include <allocator/heap.h>

#include <core/platform.h>

#include <string.h>

MEL_TEST(platform, name_matches_host)
{
    mel_platform_init(mel_alloc_heap());
    const char* name = mel_platform_name();
    MEL_REQUIRE_NOT_NULL(name);
#if MEL_PLATFORM_OSX
    MEL_EXPECT_STR_EQ(name, "macos");
#elif MEL_PLATFORM_LINUX
    MEL_EXPECT_STR_EQ(name, "linux");
#endif
    mel_platform_shutdown();
}

MEL_TEST(platform, device_class_is_desktop_on_host)
{
    mel_platform_init(mel_alloc_heap());
    u32 dc = mel_platform_device_class();
    MEL_EXPECT((dc & MEL_PLATFORM_DEVICE_DESKTOP) != 0u);
    MEL_EXPECT(!mel_platform_is_tv());
    MEL_EXPECT(!mel_platform_is_tablet());
    mel_platform_shutdown();
}

MEL_TEST(platform, sandbox_reports_none_outside_container)
{
    mel_platform_init(mel_alloc_heap());
    Mel_Platform_Sandbox sb = mel_platform_sandbox();
    MEL_EXPECT_EQ((i64)sb.flags, (i64)MEL_PLATFORM_SANDBOX_NONE);
    MEL_EXPECT(!mel_platform_sandboxed());
    mel_platform_shutdown();
}

MEL_TEST(platform, status_predicates_decode_severity)
{
    MEL_EXPECT(mel_platform_status_ok(MEL_PLATFORM_OK));
    MEL_EXPECT(mel_platform_status_failed(MEL_PLATFORM_ERROR | MEL_PLATFORM_UNAVAILABLE));
    MEL_EXPECT(mel_platform_status_denied(MEL_PLATFORM_ERROR | MEL_PLATFORM_DENIED));
    MEL_EXPECT(!mel_platform_status_ok(MEL_PLATFORM_ERROR));
    MEL_EXPECT(mel_platform_status_warned(MEL_PLATFORM_WARNED | MEL_PLATFORM_ALREADY));
    MEL_EXPECT(!mel_platform_status_warned(MEL_PLATFORM_OK));
    MEL_EXPECT(mel_platform_status_unsupported(MEL_PLATFORM_ERROR | MEL_PLATFORM_UNSUPPORTED));
    MEL_EXPECT(!mel_platform_status_unsupported(MEL_PLATFORM_ERROR | MEL_PLATFORM_UNAVAILABLE));
}

MEL_TEST(platform, screensaver_inhibit_roundtrip)
{
    mel_platform_init(mel_alloc_heap());
    MEL_EXPECT(!mel_platform_screensaver_inhibited());
    Mel_Platform_Inhibit_Result r = mel_platform_screensaver_inhibit(.reason = "test");
#if MEL_PLATFORM_OSX
    MEL_REQUIRE(!mel_platform_status_failed(r.status));
    MEL_EXPECT(mel_platform_inhibitor_valid(r.value));
    MEL_EXPECT(mel_platform_screensaver_inhibited());
    Mel_Platform_Status st = mel_platform_screensaver_uninhibit(r.value);
    MEL_EXPECT(!mel_platform_status_failed(st));
    MEL_EXPECT(!mel_platform_screensaver_inhibited());
#else
    MEL_EXPECT(mel_platform_status_failed(r.status));
    MEL_EXPECT(!mel_platform_inhibitor_valid(r.value));
    MEL_EXPECT(!mel_platform_screensaver_inhibited());
#endif
    mel_platform_shutdown();
}

MEL_TEST(platform, uninhibit_rejects_stale_handle)
{
    mel_platform_init(mel_alloc_heap());
    Mel_Platform_Inhibitor bogus = { 99, 1 };
    Mel_Platform_Status    st = mel_platform_screensaver_uninhibit(bogus);
    MEL_EXPECT(mel_platform_status_failed(st));
    MEL_EXPECT((st & MEL_PLATFORM_INVALID) != 0u);
    mel_platform_shutdown();
}

static u32  g_win32_calls;
static u32  g_win32_last_msg;
static bool g_win32_consume;

static bool win32_hook(void* hwnd, u32 msg, u64 wparam, i64 lparam, void* user)
{
    (void)hwnd;
    (void)wparam;
    (void)lparam;
    (*(u32*)user)++;
    g_win32_last_msg = msg;
    return g_win32_consume;
}

MEL_TEST(platform, win32_hook_chain_dispatches_and_consumes)
{
    mel_platform_init(mel_alloc_heap());
    MEL_EXPECT_EQ((i64)mel_platform_win32_hooks_available(), (i64)(MEL_PLATFORM_WINDOWS ? 1 : 0));

    u32 counter = 0;
    g_win32_calls = 0;
    g_win32_consume = false;
    Mel_Platform_Hook h = mel_platform_win32_add_msg_hook(win32_hook, &counter);
    MEL_EXPECT(mel_platform_hook_valid(h));

    bool consumed = mel_platform_win32_dispatch((void*)0x1, 0x100, 7, 9);
    MEL_EXPECT(!consumed);
    MEL_EXPECT_EQ((i64)counter, (i64)1);
    MEL_EXPECT_EQ((i64)g_win32_last_msg, (i64)0x100);

    g_win32_consume = true;
    consumed = mel_platform_win32_dispatch((void*)0x1, 0x101, 0, 0);
    MEL_EXPECT(consumed);
    MEL_EXPECT_EQ((i64)counter, (i64)2);

    mel_platform_win32_remove_msg_hook(h);
    consumed = mel_platform_win32_dispatch((void*)0x1, 0x102, 0, 0);
    MEL_EXPECT(!consumed);
    MEL_EXPECT_EQ((i64)counter, (i64)2);
    mel_platform_shutdown();
}

static bool x11_hook_first(void* ev, void* user)
{
    (void)ev;
    (*(u32*)user)++;
    return true;
}

static bool x11_hook_second(void* ev, void* user)
{
    (void)ev;
    (*(u32*)user)++;
    return false;
}

MEL_TEST(platform, x11_hook_chain_short_circuits_on_consume)
{
    mel_platform_init(mel_alloc_heap());
    u32 first = 0, second = 0;

    Mel_Platform_Hook h2 = mel_platform_x11_add_event_hook(x11_hook_second, &second);
    Mel_Platform_Hook h1 = mel_platform_x11_add_event_hook(x11_hook_first, &first);
    MEL_EXPECT(mel_platform_hook_valid(h1));
    MEL_EXPECT(mel_platform_hook_valid(h2));

    bool consumed = mel_platform_x11_dispatch((void*)0xab);
    MEL_EXPECT(consumed);
    MEL_EXPECT_EQ((i64)second, (i64)1);
    MEL_EXPECT_EQ((i64)first, (i64)1);

    mel_platform_x11_remove_event_hook(h1);
    mel_platform_x11_remove_event_hook(h2);
    mel_platform_shutdown();
}

MEL_TEST(platform, removed_hook_slot_is_reused_with_new_generation)
{
    mel_platform_init(mel_alloc_heap());
    u32               counter = 0;
    Mel_Platform_Hook a = mel_platform_win32_add_msg_hook(win32_hook, &counter);
    mel_platform_win32_remove_msg_hook(a);
    Mel_Platform_Hook bb = mel_platform_win32_add_msg_hook(win32_hook, &counter);
    MEL_EXPECT_EQ((i64)a.index, (i64)bb.index);
    MEL_EXPECT_NEQ((i64)a.generation, (i64)bb.generation);

    g_win32_consume = false;
    mel_platform_win32_dispatch((void*)0x1, 0x200, 0, 0);
    MEL_EXPECT_EQ((i64)counter, (i64)1);
    mel_platform_win32_remove_msg_hook(bb);
    mel_platform_shutdown();
}

MEL_TEST(platform, wayland_handles_set_and_query)
{
    mel_platform_init(mel_alloc_heap());
    MEL_EXPECT(!mel_platform_wayland_available());
    int display, surface;
    mel_platform_wayland_set_handles(&display, &surface);
    MEL_EXPECT(mel_platform_wayland_available());
    MEL_EXPECT_EQ((i64)(uintptr_t)mel_platform_wayland_display(), (i64)(uintptr_t)&display);
    MEL_EXPECT_EQ((i64)(uintptr_t)mel_platform_wayland_surface(), (i64)(uintptr_t)&surface);
    mel_platform_shutdown();
}

MEL_TEST(platform, x11_display_set_and_query)
{
    mel_platform_init(mel_alloc_heap());
    MEL_EXPECT_NULL(mel_platform_x11_display());
    int dpy;
    mel_platform_x11_set_display(&dpy);
    MEL_EXPECT_EQ((i64)(uintptr_t)mel_platform_x11_display(), (i64)(uintptr_t)&dpy);
    mel_platform_shutdown();
}
