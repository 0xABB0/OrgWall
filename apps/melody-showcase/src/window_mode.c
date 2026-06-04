#include "showcase.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <allocator/heap.h>
#include <color/rgba8.h>
#include <core/types.h>
#include <executor/executor.h>
#include <future/future.h>
#include <log/log.h>
#include <string/str8.h>

#include <app/lifecycle.h>
#include <app/subsystem.h>
#include <gui/gui.h>
#include <paint/painter.h>

#include <clipboard/clipboard.h>
#include <cpu/cpu.h>
#include <debug/assert.h>
#include <dialog/dialog.h>
#include <display/display.h>
#include <dylib/dylib.h>
#include <gamepad/gamepad.h>
#include <hid/hid.h>
#include <input/input.h>
#include <io/io.h>
#include <locale/locale.h>
#include <messagebox/messagebox.h>
#include <platform/platform.h>
#include <power/power.h>
#include <process/process.h>
#include <sensor/sensor.h>
#include <shell/shell.h>
#include <time/clock.h>
#include <time/format_prefs.h>
#include <time/nano.h>
#include <tray/menu.h>
#include <tray/tray.h>
#include <vibration/vibration.h>

#define TICK_NS    ((i64)200000000)
#define LOG_DOMAIN "showcase"

typedef struct
{
    Mel_Reactor*        reactor;
    Mel_Executor*       exec;
    Mel_Reactor_Source* timer;
    Mel_Gui_Handle      canvas;

    u64 tick;

    f32 mouse_x, mouse_y;
    u32 last_key;
    u32 mouse_buttons;

    Mel_Tray tray;
    bool     tray_on;

    bool assert_fired;

    char action_log[8][128];
    u32  action_count;

    Mel_App_Lifecycle_Subscription life_sub;

    Mel_Future* proc_pending;
    Mel_Task    proc_task;
} Window_State;

static Window_State g;

static void action(const char* fmt, ...)
{
    char    buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (g.action_count == 8)
    {
        memmove(g.action_log[0], g.action_log[1], 7 * 128);
        g.action_count--;
    }
    snprintf(g.action_log[g.action_count++], 128, "%s", buf);
    mel_log_info(LOG_DOMAIN, "%s", buf);
}

static void on_lifecycle(const Mel_App_Lifecycle_Event* ev, void* user)
{
    (void)user;
    action("app lifecycle phase=0x%x", ev->phase);
}

static Mel_Assert_Response demo_assert_handler(const Mel_Assert_Report* report, void* user)
{
    (void)report;
    (void)user;
    g.assert_fired = true;
    return MEL_ASSERT_RESPONSE_IGNORE_ONCE;
}

static void do_dialog(void)
{
    if (!mel_dialog_available())
    {
        action("dialog: no backend");
        return;
    }
    mel_dialog_open_file(.title = "Showcase: pick a file", .reactor = g.reactor, .deliver = g.exec);
    action("dialog: open-file picker requested");
}

static void do_messagebox(void)
{
    Mel_Msgbox_Button btns[] = { { .label = S8("OK"), .id = 1 }, { .label = S8("Cancel"), .id = 2 } };
    Mel_Msgbox_Result
        r = mel_msgbox_show(.title = S8("Melody Showcase"), .message = S8("Modal message box from the showcase."), .severity = MEL_MSGBOX_SEVERITY_INFO, .buttons = btns, .button_count = 2, .default_id = 1, .has_default_id = true);
    action("messagebox: chosen=%d status=0x%x", r.chosen_id, r.status);
}

static void do_tray(void)
{
    if (!mel_tray_supported())
    {
        action("tray: unsupported");
        return;
    }
    if (g.tray_on)
    {
        mel_tray_destroy(g.tray);
        g.tray = MEL_TRAY_NULL;
        g.tray_on = false;
        action("tray: removed");
        return;
    }
    Mel_Tray_Create_Result tr = mel_tray_create(.title = S8("Melody"), .tooltip = S8("Showcase"), .alloc = mel_alloc_heap(), .exec = g.exec);
    if (mel_tray_failed(tr.status))
    {
        action("tray: create failed 0x%x", tr.status);
        return;
    }
    g.tray = tr.value;
    g.tray_on = true;
    Mel_Tray_Menu m = mel_tray_menu(g.tray);
    mel_tray_item_add(m, (Mel_Tray_Item_Desc){ .label = S8("Showcase Item"), .flags = MEL_TRAY_ITEM_BUTTON | MEL_TRAY_ITEM_ENABLED });
    mel_tray_separator_add(m);
    mel_tray_item_add(m, (Mel_Tray_Item_Desc){ .label = S8("Toggle Me"), .flags = MEL_TRAY_ITEM_CHECKBOX | MEL_TRAY_ITEM_ENABLED });
    action("tray: added icon + menu (items=%u)", mel_tray_menu_count(m));
}

static void do_clipboard(void)
{
    if (!mel_clip_available())
    {
        action("clipboard: unavailable");
        return;
    }
    mel_clip_write_text(S8("melody-showcase copied this"), .exec = g.exec);
    action("clipboard: wrote text (read-back via async)");
}

static void do_shell(void)
{
    if (!mel_shell_available())
    {
        action("shell: no backend");
        return;
    }
    mel_shell_open_url(S8("https://github.com"));
    action("shell: open URL requested");
}

static void do_fs_io_storage(void)
{
    u8          buf[64];
    Mel_Stream* mem = mel_io_memory_growable(.initial_capacity = 16, .alloc = mel_alloc_heap());
    const char* payload = "windowed fs+io probe";
    mel_stream_write_sync(mem, payload, strlen(payload), MEL_IO_NO_OFFSET);
    usize wrote = mel_io_growable_len(mem);
    mel_stream_destroy(mem);
    (void)buf;
    action("fs+io: growable stream holds %zuB", wrote);
}

static void do_dylib(void)
{
    Mel_Dylib_Open_Result o = mel_dylib_open(.path = "/usr/lib/libSystem.B.dylib", .flags = MEL_DYLIB_BIND_NOW, .alloc = mel_alloc_heap());
    if (mel_dylib_status_failed(o.status))
    {
        action("dylib: open failed 0x%x", o.status);
        return;
    }
    Mel_Dylib_Symbol sym = mel_dylib_symbol(o.value, "malloc");
    action("dylib: malloc=%p status=0x%x", sym.addr, sym.status);
    mel_dylib_close(o.value);
}

static void on_proc_done(Mel_Task* self)
{
    (void)self;
    const Mel_Process_Output* o = mel_process_run_future_result(g.proc_pending);
    char                      out[64] = { 0 };
    usize                     n = o->stdout_len < sizeof out - 1 ? o->stdout_len : sizeof out - 1;
    memcpy(out, o->stdout_data, n);
    for (usize i = 0; i < n; i++)
        if (out[i] == '\n')
            out[i] = 0;
    action("process: exit=%d stdout=\"%s\" status=0x%x", o->exit_code, out, o->status);
    mel_process_run_future_release(g.proc_pending);
    g.proc_pending = NULL;
}

static void do_process(void)
{
    if (!mel_process_available())
    {
        action("process: unavailable");
        return;
    }
    if (g.proc_pending)
    {
        action("process: previous run still pending");
        return;
    }
    static const char* const argv[] = { "/bin/echo", "from-showcase", NULL };
    g.proc_pending = mel_process_run(.argv = argv, .argc = 2, .reactor = g.reactor, .deliver = g.exec, .alloc = mel_alloc_heap());
    if (!g.proc_pending)
    {
        action("process: run failed to launch");
        return;
    }
    mel_task_init(&g.proc_task, on_proc_done);
    mel_future_then(g.proc_pending, &g.proc_task, g.exec);
    action("process: /bin/echo launched (stdout via async)");
}

static void do_hid(void)
{
    mel_hid_refresh();
    action("hid: enumerated %u device(s)", mel_hid_count());
}

static void do_vibration(void)
{
    u32 n = mel_vib_count();
    if (n == 0)
    {
        action("vibration: no device");
        return;
    }
    Mel_Vib_Device d = MEL_VIB_DEVICE_NULL;
    mel_vib_list(&d, 1);
    Mel_Vib_Event       evs[] = { mel_vib_pulse(1.0f, 0.8f, 0.1f), mel_vib_pulse(0.5f, 0.4f, 0.2f) };
    Mel_Vib_Pattern     pat = { .events = evs, .count = 2, .loop = 0 };
    Mel_Vib_Play_Result r = mel_vib_play(d, &pat, .reactor = g.reactor);
    action("vibration: play status=0x%x", r.status);
}

static void do_debug(void)
{
    mel_assert_install_handler(demo_assert_handler, NULL);
    volatile int sentinel = 0;
    mel_assert_msg("windowed-demo-assertion", sentinel == 1);
    mel_assert_install_handler(NULL, NULL);
    action("debug: guarded assertion fired=%d (handled)", (int)g.assert_fired);
}

static void on_key_down(Mel_Gui_Handle h, Mel_Key key, void* user)
{
    (void)h;
    (void)user;
    g.last_key = (u32)key;
    switch (key)
    {
    case MEL_KEY_O:
        do_dialog();
        break;
    case MEL_KEY_M:
        do_messagebox();
        break;
    case MEL_KEY_T:
        do_tray();
        break;
    case MEL_KEY_C:
        do_clipboard();
        break;
    case MEL_KEY_U:
        do_shell();
        break;
    case MEL_KEY_F:
        do_fs_io_storage();
        break;
    case MEL_KEY_L:
        do_dylib();
        break;
    case MEL_KEY_P:
        do_process();
        break;
    case MEL_KEY_H:
        do_hid();
        break;
    case MEL_KEY_V:
        do_vibration();
        break;
    case MEL_KEY_D:
        do_debug();
        break;
    case MEL_KEY_Q:
        mel_reactor_quit(g.reactor);
        break;
    default:
        break;
    }
}

static void on_pointer_move(Mel_Gui_Handle h, i32 x, i32 y, void* user)
{
    (void)h;
    (void)user;
    g.mouse_x = (f32)x;
    g.mouse_y = (f32)y;
}

static void on_pointer_down(Mel_Gui_Handle h, i32 x, i32 y, void* user)
{
    (void)h;
    (void)x;
    (void)y;
    (void)user;
    g.mouse_buttons |= 1u;
}

static void on_pointer_up(Mel_Gui_Handle h, i32 x, i32 y, void* user)
{
    (void)h;
    (void)x;
    (void)y;
    (void)user;
    g.mouse_buttons &= ~1u;
}

typedef struct
{
    Mel_Painter* p;
    f32          x, y;
} Pen;

static void line(Pen* pen, mel_color8 col, const char* fmt, ...)
{
    char    buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    mel_painter_draw_text(pen->p, str8_from_cstr(buf), mel_vec2(pen->x, pen->y), col, 12.0f);
    pen->y += 15.0f;
}

static void canvas_paint(Mel_Gui_Handle h, Mel_Painter* p, i32 w, i32 height, void* user)
{
    (void)h;
    (void)user;
    mel_color8 bg = mel_color8_rgb(20, 24, 30);
    mel_color8 head = mel_color8_rgb(120, 200, 255);
    mel_color8 key = mel_color8_rgb(175, 190, 210);
    mel_color8 hot = mel_color8_rgb(255, 200, 110);
    mel_color8 act = mel_color8_rgb(150, 230, 160);

    mel_painter_clear(p, bg);

    Pen pen = { .p = p, .x = 14, .y = 12 };
    line(&pen, head, "Melody SDL-parity Showcase   tick %llu   [live %.0f ms]", (unsigned long long)g.tick, (double)TICK_NS / 1e6);
    pen.y += 4;

    Mel_Cpu_Info ci = mel_cpu_info();
    Mel_Cpu_Caps cc = mel_cpu_caps();
    line(&pen,
         key,
         "cpu      cores=%u logical=%u L1=%uB L2=%uB L3=%uB RAM=%lluMiB simd_align=%u",
         ci.core_count,
         ci.logical_count,
         ci.l1_cache_size,
         ci.l2_cache_size,
         ci.l3_cache_size,
         (unsigned long long)(cc.ram_total >> 20),
         cc.simd_align);

    Mel_Platform_Sandbox sb = mel_platform_sandbox();
    line(&pen, key, "platform name=%s sandbox=0x%x device=0x%x tablet=%d tv=%d", mel_platform_name(), sb.flags, mel_platform_device_class(), (int)mel_platform_is_tablet(), (int)mel_platform_is_tv());

    Mel_Power_Battery pb = mel_power_battery_current();
    if (pb.present)
        line(&pen, key, "power    battery %.0f%% charging=%d source=%d", (double)(pb.level * 100.0f), (int)pb.charging, (int)mel_power_source_current());
    else
        line(&pen, key, "power    battery absent (honest) source=%d", (int)mel_power_source_current());

    u64                    mono = mel_nanos_since_unspecified_epoch();
    Mel_Civil              civ = mel_civil_from_unix_ns(mel_wall_now_ns(), 0);
    Mel_Time_Format_Result fr = mel_time_format_prefs();
    char                   datebuf[32] = { 0 };
    mel_time_format_date(fr.value, civ.year, civ.month, civ.day, datebuf, sizeof datebuf);
    line(&pen, key, "time     mono=%.2fs  wall=%04d-%02u-%02uT%02u:%02u:%02uZ  locale_date=%s", (double)mono / 1e9, civ.year, civ.month, civ.day, civ.hour, civ.minute, civ.second, datebuf);

    Mel_Locale_Get_Result lp = mel_locale_primary();
    line(&pen, key, "locale   count=%u primary=%.*s", mel_locale_count(), (int)lp.value.tag.len, (const char*)lp.value.tag.data);

    line(&pen, key, "display  monitors=%u", mel_display_count());

    line(&pen, key, "input    devices=%u mouse=(%.0f,%.0f) buttons=0x%x last_key=%u", mel_input_count(), (double)g.mouse_x, (double)g.mouse_y, g.mouse_buttons, g.last_key);

    u32 jn = mel_joystick_count();
    if (jn == 0)
        line(&pen, key, "gamepad  none connected (honest)");
    else
    {
        Mel_Joystick j = MEL_JOYSTICK_NULL;
        mel_joystick_list(&j, 1);
        Mel_Joystick_State_Result st = mel_joystick_poll(j);
        line(&pen, key, "gamepad  joysticks=%u axes=%u buttons=%u", jn, st.value.axis_count, st.value.button_count);
    }

    u32 sn = mel_sensor_count();
    if (sn == 0)
        line(&pen, key, "sensor   none present (honest)");
    else
        line(&pen, key, "sensor   sensors=%u", sn);

    line(&pen, key, "hid      devices=%u", mel_hid_count());

    line(&pen, key, "tray     %s  vibration devices=%u  clipboard=%d  process=%d", g.tray_on ? "ON" : "off", mel_vib_count(), (int)mel_clip_available(), (int)mel_process_available());

    pen.y += 6;
    line(&pen, hot, "keys: O dialog  M messagebox  T tray  C clipboard  U shell(url)");
    line(&pen, hot, "      F fs+io+storage  L dylib  P process  H hid  V vibration  D debug-assert  Q quit");

    pen.y += 6;
    f32 ey = (f32)height - 14 - (f32)(g.action_count + 1) * 15.0f;
    if (ey < pen.y)
        ey = pen.y;
    Pen ap = { .p = p, .x = 14, .y = ey };
    mel_painter_draw_line(p, mel_vec2(0, ey - 4), mel_vec2((f32)w, ey - 4), mel_color8_rgb(60, 70, 82), 1.0f);
    line(&ap, act, "action log:");
    for (u32 i = 0; i < g.action_count; i++)
        line(&ap, act, "  %s", g.action_log[i]);
}

static bool tick(void* user)
{
    (void)user;
    g.tick++;
    mel_display_refresh();
    mel_input_refresh();
    mel_joystick_refresh();
    mel_sensor_refresh();
    if (!mel_gui_handle_is_none(g.canvas))
        mel_gui_invalidate(g.canvas);
    return true;
}

static void build_screen(Mel_Gui_Handle frame, void* user)
{
    (void)user;
    mel_gui_set_text(frame, S8("Melody Showcase"));
    mel_gui_set_layout(frame, mel_column_layout(.spacing = 0, .margin = 0, .cross_align = MEL_ALIGN_STRETCH));
    g.canvas = mel_canvas_create(frame,
                                 .on_.on_paint = canvas_paint,
                                 .keyboard.on_key_down = on_key_down,
                                 .pointer.on_pointer_move = on_pointer_move,
                                 .pointer.on_pointer_down = on_pointer_down,
                                 .pointer.on_pointer_up = on_pointer_up,
                                 .layoutable = { .preferred_w = 940, .preferred_h = 660, .weight = 1 });

    g.timer = mel_reactor_timer_new(TICK_NS, tick, NULL);
    if (g.timer)
        mel_reactor_source_attach(g.reactor, g.timer);
}

void showcase_window_setup(Mel_Reactor* reactor)
{
    memset(&g, 0, sizeof g);
    g.canvas = MEL_GUI_HANDLE_NONE;
    g.tray = MEL_TRAY_NULL;
    g.reactor = reactor;
    g.exec = mel_reactor_executor(reactor);

    mel_gui_init(reactor);
    mel_platform_init(mel_alloc_heap());
    mel_display_init(mel_alloc_heap());
    mel_locale_init(mel_alloc_heap());
    mel_time_format_init(mel_alloc_heap());
    mel_input_init(mel_alloc_heap());
    mel_joystick_init(mel_alloc_heap());
    mel_sensor_init(mel_alloc_heap());
    mel_hid_init(mel_alloc_heap());
    mel_tray_init(mel_alloc_heap(), g.exec);
    mel_vib_init(mel_alloc_heap(), reactor);
    mel_dialog_init(mel_alloc_heap(), reactor);
    mel_shell_init(mel_alloc_heap(), reactor);
    mel_clip_init(mel_alloc_heap(), reactor);

    g.life_sub = mel_app_lifecycle_subscribe(g.exec, on_lifecycle, NULL);
    action("showcase started — press keys to trigger actions");

    mel_app_register_screen(S8("showcase"), build_screen, NULL);
    mel_app_present(S8("showcase"), NULL);
}
