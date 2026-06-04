#include <test/test.h>

#include <tray/tray.h>
#include <tray/menu.h>
#include <tray/events.h>
#include <tray/provider.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <executor/executor.h>
#include <string/str8.h>

#include <string.h>

#include "../src/tray_internal.h"

typedef struct
{
    u32 created;
    u32 destroyed;
    u32 items_added;
    u32 items_removed;
    u32 labels_set;
    u32 flags_set;
    u32 menus_created;
    u32 images_set;
    u32 tooltips_set;
    u32 last_flags;
    u64 last_item_token;
} Fake_State;

static Fake_State fk;

static bool fake_supported(void* u)
{
    (void)u;
    return true;
}
static Mel_Tray_Status fake_create(void* u, const Mel_Tray_Lowered* lw)
{
    (void)u;
    (void)lw;
    fk.created++;
    return MEL_TRAY_OK;
}
static void fake_destroy(void* u, u64 tok)
{
    (void)u;
    (void)tok;
    fk.destroyed++;
}
static Mel_Tray_Status fake_set_image(void* u, u64 tok, Mel_Tray_Image im)
{
    (void)u;
    (void)tok;
    (void)im;
    fk.images_set++;
    return MEL_TRAY_OK;
}
static Mel_Tray_Status fake_set_tooltip(void* u, u64 tok, str8 s)
{
    (void)u;
    (void)tok;
    (void)s;
    fk.tooltips_set++;
    return MEL_TRAY_OK;
}
static Mel_Tray_Status fake_set_title(void* u, u64 tok, str8 s)
{
    (void)u;
    (void)tok;
    (void)s;
    return MEL_TRAY_OK;
}
static Mel_Tray_Status fake_set_visible(void* u, u64 tok, bool v)
{
    (void)u;
    (void)tok;
    (void)v;
    return MEL_TRAY_OK;
}
static Mel_Tray_Status fake_menu_create(void* u, u64 tok)
{
    (void)u;
    (void)tok;
    fk.menus_created++;
    return MEL_TRAY_OK;
}
static void fake_menu_destroy(void* u, u64 tok)
{
    (void)u;
    (void)tok;
}
static Mel_Tray_Status fake_item_add(void* u, const Mel_Tray_Item_Lowered* lw)
{
    (void)u;
    fk.items_added++;
    fk.last_item_token = lw->token;
    fk.last_flags = lw->flags;
    return MEL_TRAY_OK;
}
static void fake_item_remove(void* u, u64 tok)
{
    (void)u;
    (void)tok;
    fk.items_removed++;
}
static Mel_Tray_Status fake_item_set_label(void* u, u64 tok, str8 s)
{
    (void)u;
    (void)tok;
    (void)s;
    fk.labels_set++;
    return MEL_TRAY_OK;
}
static Mel_Tray_Status fake_item_set_flags(void* u, u64 tok, Mel_Tray_Item_Flags f)
{
    (void)u;
    (void)tok;
    fk.flags_set++;
    fk.last_flags = f;
    return MEL_TRAY_OK;
}
static void* fake_native(void* u, u64 tok)
{
    (void)u;
    return (void*)(usize)tok;
}

static Mel_Tray_Provider install_fake(void)
{
    static const Mel_Tray_Provider_Desc desc = {
        .name = "fake",
        .supported = fake_supported,
        .create = fake_create,
        .destroy = fake_destroy,
        .set_image = fake_set_image,
        .set_tooltip = fake_set_tooltip,
        .set_title = fake_set_title,
        .set_visible = fake_set_visible,
        .menu_create = fake_menu_create,
        .menu_destroy = fake_menu_destroy,
        .item_add = fake_item_add,
        .item_remove = fake_item_remove,
        .item_set_label = fake_item_set_label,
        .item_set_flags = fake_item_set_flags,
        .native = fake_native,
    };
    Mel_Tray_Provider p = mel_tray_provider_register(&desc);
    mel_tray__force_provider(p);
    return p;
}

static void begin(void)
{
    memset(&fk, 0, sizeof fk);
    mel_tray__init_bare(mel_alloc_heap(), mel_executor_inline());
    install_fake();
}

MEL_TEST(tray, null_handle_is_dead)
{
    Mel_Tray null = MEL_TRAY_NULL;
    MEL_EXPECT(!mel_tray_alive(null));
    MEL_EXPECT(mel_tray_equal(null, null));
}

MEL_TEST(tray, dead_handle_set_is_loud_error)
{
    begin();
    Mel_Tray bogus = { .h = { .index = 9999, .generation = 7 } };
    MEL_EXPECT(!mel_tray_alive(bogus));
    Mel_Tray_Status s = mel_tray_set_tooltip(bogus, S8("nope"));
    MEL_EXPECT(mel_tray_failed(s));
    MEL_EXPECT((s & MEL_TRAY_ERR_DEAD_HANDLE) != 0);
    mel_tray_shutdown();
}

MEL_TEST(tray, create_and_destroy_calls_provider)
{
    begin();
    Mel_Tray_Create_Result r = mel_tray_create(.tooltip = S8("hi"));
    MEL_REQUIRE(!mel_tray_failed(r.status));
    MEL_REQUIRE(mel_tray_alive(r.value));
    MEL_EXPECT_EQ(fk.created, 1u);
    MEL_EXPECT_EQ(fk.menus_created, 1u);

    Mel_Tray_Menu m = mel_tray_menu(r.value);
    MEL_EXPECT(mel_tray_menu_alive(m));

    mel_tray_destroy(r.value);
    MEL_EXPECT(!mel_tray_alive(r.value));
    MEL_EXPECT_EQ(fk.destroyed, 1u);
    mel_tray_shutdown();
}

MEL_TEST(tray, no_provider_create_fails_loud)
{
    memset(&fk, 0, sizeof fk);
    mel_tray__init_bare(mel_alloc_heap(), NULL);
    Mel_Tray_Create_Result r = mel_tray_create(.tooltip = S8("x"));
    MEL_EXPECT(mel_tray_failed(r.status));
    MEL_EXPECT((r.status & MEL_TRAY_ERR_NO_PROVIDER) != 0);
    MEL_EXPECT(!mel_tray_alive(r.value));
    mel_tray_shutdown();
}

MEL_TEST(tray, item_add_insert_remove)
{
    begin();
    Mel_Tray_Create_Result r = mel_tray_create();
    Mel_Tray_Menu          m = mel_tray_menu(r.value);

    Mel_Tray_Item_Result a = mel_tray_item_add(m, (Mel_Tray_Item_Desc){ .label = S8("A"), .flags = MEL_TRAY_ITEM_BUTTON | MEL_TRAY_ITEM_ENABLED });
    Mel_Tray_Item_Result b = mel_tray_item_add(m, (Mel_Tray_Item_Desc){ .label = S8("B"), .flags = MEL_TRAY_ITEM_BUTTON | MEL_TRAY_ITEM_ENABLED });
    MEL_REQUIRE(mel_tray_item_alive(a.value));
    MEL_REQUIRE(mel_tray_item_alive(b.value));
    MEL_EXPECT_EQ(mel_tray_menu_count(m), 2u);

    Mel_Tray_Item_Result c = mel_tray_item_insert(m, 1, (Mel_Tray_Item_Desc){ .label = S8("C"), .flags = MEL_TRAY_ITEM_BUTTON });
    MEL_REQUIRE(mel_tray_item_alive(c.value));
    MEL_EXPECT_EQ(mel_tray_menu_count(m), 3u);
    MEL_EXPECT_EQ(fk.items_added, 3u);

    MEL_EXPECT_EQ(mel_tray_item_remove(c.value), MEL_TRAY_OK);
    MEL_EXPECT(!mel_tray_item_alive(c.value));
    MEL_EXPECT_EQ(mel_tray_menu_count(m), 2u);
    MEL_EXPECT_EQ(fk.items_removed, 1u);

    mel_tray_destroy(r.value);
    mel_tray_shutdown();
}

MEL_TEST(tray, separator_defaults_kind_to_button_when_unset)
{
    begin();
    Mel_Tray_Create_Result r = mel_tray_create();
    Mel_Tray_Menu          m = mel_tray_menu(r.value);

    Mel_Tray_Item_Result sep = mel_tray_separator_add(m);
    MEL_REQUIRE(mel_tray_item_alive(sep.value));
    MEL_EXPECT((mel_tray_item_flags(sep.value) & MEL_TRAY_ITEM_SEPARATOR) != 0);

    Mel_Tray_Item_Result plain = mel_tray_item_add(m, (Mel_Tray_Item_Desc){ .label = S8("p") });
    MEL_EXPECT((mel_tray_item_flags(plain.value) & MEL_TRAY_ITEM_BUTTON) != 0);

    mel_tray_destroy(r.value);
    mel_tray_shutdown();
}

MEL_TEST(tray, update_label_enabled_checked)
{
    begin();
    Mel_Tray_Create_Result r = mel_tray_create();
    Mel_Tray_Menu          m = mel_tray_menu(r.value);

    Mel_Tray_Item_Result chk = mel_tray_item_add(m, (Mel_Tray_Item_Desc){ .label = S8("toggle"), .flags = MEL_TRAY_ITEM_CHECKBOX | MEL_TRAY_ITEM_ENABLED });
    MEL_REQUIRE(mel_tray_item_alive(chk.value));

    MEL_EXPECT_EQ(mel_tray_item_set_label(chk.value, S8("renamed")), MEL_TRAY_OK);
    MEL_EXPECT_EQ(fk.labels_set, 1u);

    MEL_EXPECT(mel_tray_item_enabled(chk.value));
    MEL_EXPECT_EQ(mel_tray_item_set_enabled(chk.value, false), MEL_TRAY_OK);
    MEL_EXPECT(!mel_tray_item_enabled(chk.value));

    MEL_EXPECT(!mel_tray_item_checked(chk.value));
    MEL_EXPECT_EQ(mel_tray_item_set_checked(chk.value, true), MEL_TRAY_OK);
    MEL_EXPECT(mel_tray_item_checked(chk.value));

    mel_tray_destroy(r.value);
    mel_tray_shutdown();
}

MEL_TEST(tray, set_checked_on_button_is_loud_error)
{
    begin();
    Mel_Tray_Create_Result r = mel_tray_create();
    Mel_Tray_Menu          m = mel_tray_menu(r.value);
    Mel_Tray_Item_Result   btn = mel_tray_item_add(m, (Mel_Tray_Item_Desc){ .label = S8("b"), .flags = MEL_TRAY_ITEM_BUTTON });
    Mel_Tray_Status        s = mel_tray_item_set_checked(btn.value, true);
    MEL_EXPECT(mel_tray_failed(s));
    MEL_EXPECT((s & MEL_TRAY_ERR_INVALID_ARG) != 0);
    mel_tray_destroy(r.value);
    mel_tray_shutdown();
}

MEL_TEST(tray, nested_submenu)
{
    begin();
    Mel_Tray_Create_Result r = mel_tray_create();
    Mel_Tray_Menu          m = mel_tray_menu(r.value);

    Mel_Tray_Submenu_Result sub = mel_tray_submenu_add(m, S8("More"));
    MEL_REQUIRE(!mel_tray_failed(sub.status));
    MEL_REQUIRE(mel_tray_menu_alive(sub.value));
    MEL_EXPECT_EQ(mel_tray_menu_count(m), 1u);

    Mel_Tray_Item_Result inner = mel_tray_item_add(sub.value, (Mel_Tray_Item_Desc){ .label = S8("deep"), .flags = MEL_TRAY_ITEM_BUTTON });
    MEL_REQUIRE(mel_tray_item_alive(inner.value));
    MEL_EXPECT_EQ(mel_tray_menu_count(sub.value), 1u);

    mel_tray_destroy(r.value);
    MEL_EXPECT(!mel_tray_menu_alive(sub.value));
    MEL_EXPECT(!mel_tray_item_alive(inner.value));
    mel_tray_shutdown();
}

typedef struct
{
    u32           hits;
    Mel_Tray_Item last;
} Click_Sink;

static void on_click(Mel_Tray_Item item, void* user)
{
    Click_Sink* s = (Click_Sink*)user;
    s->hits++;
    s->last = item;
}

MEL_TEST(tray, dispatch_item_clicked_fires_callback_and_toggles_checkbox)
{
    begin();
    Mel_Tray_Create_Result r = mel_tray_create();
    Mel_Tray_Menu          m = mel_tray_menu(r.value);

    Click_Sink           sink = { 0 };
    Mel_Tray_Item_Result chk = mel_tray_item_add(m, (Mel_Tray_Item_Desc){ .label = S8("c"), .flags = MEL_TRAY_ITEM_CHECKBOX | MEL_TRAY_ITEM_ENABLED, .on_activate = on_click, .user = &sink });
    MEL_REQUIRE(mel_tray_item_alive(chk.value));

    MEL_EXPECT(!mel_tray_item_checked(chk.value));
    mel_tray__dispatch_item_clicked(mel_slotmap_handle_pack64(chk.value.h));
    MEL_EXPECT_EQ(sink.hits, 1u);
    MEL_EXPECT(mel_tray_item_equal(sink.last, chk.value));
    MEL_EXPECT(mel_tray_item_checked(chk.value));

    mel_tray__dispatch_item_clicked(mel_slotmap_handle_pack64(chk.value.h));
    MEL_EXPECT_EQ(sink.hits, 2u);
    MEL_EXPECT(!mel_tray_item_checked(chk.value));

    mel_tray_destroy(r.value);
    mel_tray_shutdown();
}

MEL_TEST(tray, events_poll_collects_activation)
{
    begin();
    Mel_Tray_Create_Result r = mel_tray_create();

    Mel_Tray_Event drain[16];
    mel_tray_poll_events(drain, 16);

    mel_tray__dispatch_activate(mel_slotmap_handle_pack64(r.value.h), MEL_TRAY_BUTTON_RIGHT);

    u32 got = mel_tray_poll_events(drain, 16);
    MEL_EXPECT_EQ(got, 1u);
    MEL_EXPECT_EQ(drain[0].kind, (Mel_Tray_Event_Kind)MEL_TRAY_EVENT_ACTIVATED);
    MEL_EXPECT(mel_tray_equal(drain[0].tray, r.value));
    MEL_EXPECT((drain[0].buttons & MEL_TRAY_BUTTON_RIGHT) != 0);

    mel_tray_destroy(r.value);
    mel_tray_shutdown();
}

typedef struct
{
    u32 activations;
    u32 clicks;
} Push_Sink;

static void push_cb(const Mel_Tray_Event* ev, void* user)
{
    Push_Sink* s = (Push_Sink*)user;
    if (ev->kind == (Mel_Tray_Event_Kind)MEL_TRAY_EVENT_ACTIVATED)
        s->activations++;
    else if (ev->kind == (Mel_Tray_Event_Kind)MEL_TRAY_EVENT_ITEM_CLICKED)
        s->clicks++;
}

MEL_TEST(tray, subscribe_push_delivers_events)
{
    begin();
    Mel_Tray_Create_Result r = mel_tray_create();
    Mel_Tray_Menu          m = mel_tray_menu(r.value);
    Mel_Tray_Item_Result   it = mel_tray_item_add(m, (Mel_Tray_Item_Desc){ .label = S8("z"), .flags = MEL_TRAY_ITEM_BUTTON });

    Mel_Tray_Event drain[16];
    mel_tray_poll_events(drain, 16);

    Push_Sink             sink = { 0 };
    Mel_Tray_Subscription sub = mel_tray_subscribe(mel_executor_inline(), push_cb, &sink);
    MEL_REQUIRE(mel_slotmap_handle_valid(sub.handle));

    mel_tray__dispatch_activate(mel_slotmap_handle_pack64(r.value.h), MEL_TRAY_BUTTON_LEFT);
    mel_tray__dispatch_item_clicked(mel_slotmap_handle_pack64(it.value.h));
    MEL_EXPECT_EQ(sink.activations, 1u);
    MEL_EXPECT_EQ(sink.clicks, 1u);

    mel_tray_unsubscribe(sub);
    mel_tray_destroy(r.value);
    mel_tray_shutdown();
}

MEL_TEST(tray, subscribe_without_executor_is_loud_null)
{
    memset(&fk, 0, sizeof fk);
    mel_tray__init_bare(mel_alloc_heap(), NULL);
    install_fake();
    Push_Sink             sink = { 0 };
    Mel_Tray_Subscription sub = mel_tray_subscribe(NULL, push_cb, &sink);
    MEL_EXPECT(!mel_slotmap_handle_valid(sub.handle));
    mel_tray_shutdown();
}

MEL_TEST(tray, set_image_and_tooltip_reach_provider)
{
    begin();
    Mel_Tray_Create_Result r = mel_tray_create();

    u8             rgba[4 * 4] = { 0 };
    Mel_Tray_Image img = { .rgba = rgba, .width = 2, .height = 2 };
    MEL_EXPECT_EQ(mel_tray_set_image(r.value, img), MEL_TRAY_OK);
    MEL_EXPECT_EQ(fk.images_set, 1u);

    MEL_EXPECT_EQ(mel_tray_set_tooltip(r.value, S8("tip")), MEL_TRAY_OK);
    MEL_EXPECT_EQ(fk.tooltips_set, 1u);

    mel_tray_destroy(r.value);
    mel_tray_shutdown();
}
