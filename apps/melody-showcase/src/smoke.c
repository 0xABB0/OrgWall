#include "showcase.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <allocator/heap.h>
#include <core/types.h>
#include <boot/boot.h>
#include <executor/executor.h>
#include <future/future.h>
#include <string/str8.h>
#include <vat/vat.h>

#include <clipboard/clipboard.h>
#include <cpu/cpu.h>
#include <debug/assert.h>
#include <dialog/dialog.h>
#include <display/display.h>
#include <dylib/dylib.h>
#include <fs/fs.h>
#include <fs/paths.h>
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
#include <storage/storage.h>
#include <time/clock.h>
#include <time/format_prefs.h>
#include <time/nano.h>
#include <tray/menu.h>
#include <tray/tray.h>
#include <vibration/vibration.h>

static void emit(const char* module, const char* fmt, ...)
{
    char    buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    printf("%s: %s\n", module, buf);
    fflush(stdout);
}

typedef struct
{
    Mel_Vat*      vat;
    Mel_Executor* exec;

    Mel_Fs*      fs;
    Mel_Storage* storage;

    u32 step;

    Mel_Future* pending;
    Mel_Task    cont;
    Mel_Task    kick;

    Mel_Process* proc;
    Mel_Stream*  proc_out;

    char fs_path[512];
    char storage_root[512];
    char proc_out_path[512];

    bool assert_fired;
} Smoke;

static Smoke g_smoke;

static Mel_Assert_Response smoke_assert_handler(const Mel_Assert_Report* report, void* user)
{
    (void)report;
    Smoke* s = (Smoke*)user;
    s->assert_fired = true;
    return MEL_ASSERT_RESPONSE_IGNORE_ONCE;
}

static void sync_modules(Smoke* s)
{
    {
        Mel_Cpu_Info info = mel_cpu_info();
        Mel_Cpu_Caps caps = mel_cpu_caps();
        const char*  simd = mel_cpu_has(caps.features, MEL_CPU_FEATURE_NEON) ? "NEON" : mel_cpu_has(caps.features, MEL_CPU_FEATURE_AVX2) ? "AVX2" : mel_cpu_has(caps.features, MEL_CPU_FEATURE_SSE2) ? "SSE2" : "baseline";
        emit("cpu",
             "cores=%u logical=%u L1=%uB L2=%uB L3=%uB line=%uB RAM=%lluMiB SIMD=%s align=%u",
             info.core_count,
             info.logical_count,
             info.l1_cache_size,
             info.l2_cache_size,
             info.l3_cache_size,
             info.cache_line_size,
             (unsigned long long)(caps.ram_total >> 20),
             simd,
             caps.simd_align);
    }

    {
        mel_platform_init(mel_alloc_heap());
        Mel_Platform_Sandbox sb = mel_platform_sandbox();
        emit("platform",
             "name=%s sandbox=0x%x app_id=%s device_class=0x%x tablet=%d tv=%d",
             mel_platform_name(),
             sb.flags,
             sb.app_id ? sb.app_id : "(none)",
             mel_platform_device_class(),
             (int)mel_platform_is_tablet(),
             (int)mel_platform_is_tv());
    }

    {
        Mel_Power_Caps    pc = mel_power_caps();
        Mel_Power_Battery b = mel_power_battery_current();
        Mel_Power_Source  src = mel_power_source_current();
        if (b.present)
            emit("power", "source=%d battery present level=%.0f%% charging=%d", (int)src, (double)(b.level * 100.0f), (int)b.charging);
        else
            emit("power", "source=%d battery absent (caps: src=%d profile=%d batt=%d) — honest-absent on host", (int)src, (int)pc.power_source_present, (int)pc.profile_present, (int)pc.battery_present);
    }

    {
        u64         mono = mel_nanos_since_unspecified_epoch();
        mel_nanosec wall = mel_wall_now_ns();
        Mel_Civil   civ = mel_civil_from_unix_ns(wall, 0);
        mel_time_format_init(mel_alloc_heap());
        Mel_Time_Format_Result fr = mel_time_format_prefs();
        char                   datebuf[32] = { 0 };
        mel_time_format_date(fr.value, civ.year, civ.month, civ.day, datebuf, sizeof datebuf);
        emit("time",
             "monotonic=%.3fs wall(UTC)=%04d-%02u-%02uT%02u:%02u:%02uZ locale_date=%s order=0x%x clock=0x%x",
             (double)mono / 1e9,
             civ.year,
             civ.month,
             civ.day,
             civ.hour,
             civ.minute,
             civ.second,
             datebuf,
             fr.value.date_order,
             fr.value.clock);
        mel_time_format_shutdown();
    }

    {
        mel_locale_init(mel_alloc_heap());
        mel_locale_refresh();
        u32                   n = mel_locale_count();
        Mel_Locale_Get_Result pr = mel_locale_primary();
        char                  tags[256] = { 0 };
        u32                   shown = n < 4 ? n : 4;
        for (u32 i = 0; i < shown; i++)
        {
            Mel_Locale_Get_Result r = mel_locale_at(i);
            if (i)
                strncat(tags, ",", sizeof tags - strlen(tags) - 1);
            strncat(tags, (const char*)r.value.tag.data, sizeof tags - strlen(tags) - 1);
        }
        emit("locale", "count=%u primary=%.*s preferred=[%s]", n, (int)pr.value.tag.len, (const char*)pr.value.tag.data, tags);
        mel_locale_shutdown();
    }

    {
        mel_display_init(mel_alloc_heap());
        mel_display_refresh();
        u32 n = mel_display_count();
        emit("display", "monitors=%u", n);
        mel_display_shutdown();
    }

    {
        mel_input_init(mel_alloc_heap());
        mel_input_refresh();
        u32 n = mel_input_count();
        if (n == 0)
            emit("input", "no input devices enumerated on host — honest-absent");
        else
        {
            Mel_Input_Device d = MEL_INPUT_DEVICE_NULL;
            mel_input_list(&d, 1);
            Mel_Input_Describe_Result dr = mel_input_describe(d);
            emit("input", "devices=%u first=\"%.*s\" caps=0x%x keys=%u buttons=%u", n, (int)dr.value.name.len, (const char*)dr.value.name.data, dr.value.caps, dr.value.key_count, dr.value.button_count);
        }
        mel_input_shutdown();
    }

    {
        mel_joystick_init(mel_alloc_heap());
        mel_joystick_refresh();
        u32 n = mel_joystick_count();
        if (n == 0)
            emit("gamepad", "no joystick/gamepad connected — honest-absent");
        else
        {
            Mel_Joystick j = MEL_JOYSTICK_NULL;
            mel_joystick_list(&j, 1);
            Mel_Joystick_Describe_Result dr = mel_joystick_describe(j);
            emit("gamepad", "joysticks=%u first=\"%.*s\" gamepad_mapped=%d axes=%u buttons=%u", n, (int)dr.value.name.len, (const char*)dr.value.name.data, (int)mel_gamepad_supported(j), dr.value.axis_count, dr.value.button_count);
        }
        mel_joystick_shutdown();
    }

    {
        mel_sensor_init(mel_alloc_heap());
        mel_sensor_refresh();
        u32 n = mel_sensor_count();
        if (n == 0)
            emit("sensor", "no accel/gyro sensor present — honest-absent");
        else
        {
            Mel_Sensor s = MEL_SENSOR_NULL;
            mel_sensor_list(&s, 1);
            Mel_Sensor_Describe_Result dr = mel_sensor_describe(s);
            emit("sensor", "sensors=%u first=\"%.*s\" accel=%d gyro=%d", n, (int)dr.value.name.len, (const char*)dr.value.name.data, (int)dr.value.caps.has_accel, (int)dr.value.caps.has_gyro);
        }
        mel_sensor_shutdown();
    }

    {
        mel_hid_init(mel_alloc_heap());
        mel_hid_refresh();
        u32 n = mel_hid_count();
        if (n == 0)
            emit("hid", "no HID devices enumerated (no backend or none present) — honest-absent");
        else
        {
            Mel_Hid_Device d = MEL_HID_DEVICE_NULL;
            mel_hid_list(&d, 1);
            Mel_Hid_Describe_Result dr = mel_hid_describe(d);
            emit("hid", "devices=%u first vid=0x%04x pid=0x%04x product=\"%s\"", n, dr.value.vendor_id, dr.value.product_id, dr.value.product);
        }
        mel_hid_shutdown();
    }

    {
        Mel_Dylib_Open_Result o = mel_dylib_open(.path = "/usr/lib/libSystem.B.dylib", .flags = MEL_DYLIB_BIND_NOW, .alloc = mel_alloc_heap());
        if (mel_dylib_status_failed(o.status))
            emit("dylib", "open libSystem failed status=0x%x available=%d", o.status, (int)mel_dylib_available());
        else
        {
            Mel_Dylib_Symbol sym = mel_dylib_symbol(o.value, "malloc");
            emit("dylib", "opened %s resolved malloc=%p status=0x%x", mel_dylib_path(o.value), sym.addr, sym.status);
            mel_dylib_close(o.value);
        }
    }

    {
        u8            buf[64];
        Mel_Stream*   mem = mel_io_memory_fixed(.buffer = buf, .len = sizeof buf, .alloc = mel_alloc_heap());
        const char*   payload = "io-roundtrip-0123456789";
        Mel_IO_Result w = mel_stream_write_sync(mem, payload, strlen(payload), 0);
        u8            rd[64] = { 0 };
        Mel_IO_Result r = mel_stream_read_sync(mem, rd, strlen(payload), 0);
        emit("io", "memory stream wrote=%zu read=%zu match=%d bytes=\"%.*s\"", w.bytes_transferred, r.bytes_transferred, (int)(memcmp(payload, rd, strlen(payload)) == 0), (int)r.bytes_transferred, (const char*)rd);
        mel_stream_destroy(mem);
    }

    {
        emit("messagebox", "backend available=%d (modal suppressed in --smoke)", (int)mel_msgbox_available());
    }

    {
        mel_tray_init(mel_alloc_heap(), s->exec);
        bool sup = mel_tray_supported();
        if (!sup)
            emit("tray", "tray unsupported on host — honest-absent");
        else
        {
            Mel_Tray_Create_Result tr = mel_tray_create(.title = S8("Showcase"), .tooltip = S8("Melody"), .alloc = mel_alloc_heap(), .exec = s->exec);
            if (mel_tray_failed(tr.status))
                emit("tray", "create failed status=0x%x", tr.status);
            else
            {
                Mel_Tray_Menu m = mel_tray_menu(tr.value);
                emit("tray", "supported, created tray + menu handle alive=%d (menu mutation needs the app run loop; exercised in windowed mode)", (int)mel_tray_menu_alive(m));
                mel_tray_destroy(tr.value);
            }
        }
        mel_tray_shutdown();
    }

    {
        mel_vib_init(mel_alloc_heap(), s->vat);
        u32 n = mel_vib_count();
        if (n == 0)
            emit("vibration", "no haptic device present — honest-absent");
        else
        {
            Mel_Vib_Device d = MEL_VIB_DEVICE_NULL;
            mel_vib_list(&d, 1);
            Mel_Vib_Describe_Result dr = mel_vib_describe(d);
            emit("vibration", "devices=%u first=\"%.*s\"", n, (int)dr.value.name.len, (const char*)dr.value.name.data);
        }
    }

    {
        mel_assert_install_handler(smoke_assert_handler, s);
        volatile int sentinel = 0;
        mel_assert_msg("guarded-demo-assertion", sentinel == 1);
        mel_assert_install_handler(NULL, NULL);
        emit("debug", "guarded assertion fired=%d handled-without-crash", (int)s->assert_fired);
    }
}

static void smoke_finish(Smoke* s)
{
    emit("app", "lifecycle: vat run driven to completion (smoke sequence done)");
    if (s->fs)
        mel_fs_destroy(s->fs);
    if (s->storage)
        mel_storage_destroy(s->storage);
    mel_dialog_shutdown();
    mel_shell_shutdown();
    mel_clip_shutdown();
    mel_app_set_exit_code(0);
    mel_vat_quit(s->vat);
}

static void smoke_advance(Mel_Task* self);

static void smoke_chain(Smoke* s, Mel_Future* f)
{
    s->pending = f;
    mel_task_init(&s->cont, smoke_advance);
    mel_future_then(f, &s->cont, s->exec);
}

static void smoke_advance(Mel_Task* self)
{
    (void)self;
    Smoke* s = &g_smoke;

    switch (s->step)
    {
    case 0:
    {
        const char* text = "fs+io+storage temp roundtrip payload";
        snprintf(s->fs_path, sizeof s->fs_path, "/tmp/melody_showcase_%llu.txt", (unsigned long long)mel_nanos_since_unspecified_epoch());
        s->step = 1;
        smoke_chain(s, mel_fs_write_file(s->fs, str8_from_cstr(s->fs_path), .data = (const u8*)text, .len = strlen(text), .deliver = s->exec));
        return;
    }
    case 1:
    {
        const Mel_Fs_Void_Result* wr = mel_fs_future_void(s->pending);
        Mel_Fs_Status             ws = wr->status;
        mel_fs_future_release(s->pending);
        if (mel_fs_failed(ws))
        {
            emit("fs", "write failed status=0x%x", ws);
            emit("storage", "skipped — fs precondition failed");
            s->step = 99;
            smoke_finish(s);
            return;
        }
        s->step = 2;
        smoke_chain(s, mel_fs_read_file(s->fs, str8_from_cstr(s->fs_path), .deliver = s->exec));
        return;
    }
    case 2:
    {
        const Mel_Fs_Bytes_Result* rd = mel_fs_future_bytes(s->pending);
        emit("fs", "wrote+read temp file %s bytes=%zu status=0x%x", s->fs_path, rd->len, rd->status);
        mel_fs_future_release(s->pending);
        s->step = 3;
        const char* sdata = "storage-relative-key payload";
        smoke_chain(s, mel_storage_write(s->storage, S8("showcase/value.bin"), .data = (const u8*)sdata, .len = strlen(sdata), .deliver = s->exec));
        return;
    }
    case 3:
    {
        const Mel_Storage_Void_Result* wr = mel_storage_future_void(s->pending);
        Mel_Storage_Status             ws = wr->status;
        mel_storage_future_release(s->pending);
        if (mel_storage_failed(ws))
        {
            emit("storage", "write failed status=0x%x", ws);
            s->step = 50;
            smoke_advance(self);
            return;
        }
        s->step = 4;
        smoke_chain(s, mel_storage_read(s->storage, S8("showcase/value.bin"), .deliver = s->exec));
        return;
    }
    case 4:
    {
        const Mel_Storage_Bytes* rb = mel_storage_future_bytes(s->pending);
        emit("storage", "wrote+read key showcase/value.bin under %s bytes=%zu status=0x%x", s->storage_root, rb->len, rb->status);
        mel_storage_future_release(s->pending);
        if (!mel_process_available())
        {
            emit("process", "process backend unavailable on host — honest-absent");
            s->step = 50;
            smoke_advance(self);
            return;
        }
        snprintf(s->proc_out_path, sizeof s->proc_out_path, "/tmp/melody_showcase_proc_%llu.txt", (unsigned long long)mel_nanos_since_unspecified_epoch());
        Mel_IO_File_Open_Result of = mel_io_file_open(.path = s->proc_out_path, .flags = MEL_IO_FILE_READ | MEL_IO_FILE_WRITE | MEL_IO_FILE_CREATE | MEL_IO_FILE_TRUNCATE, .alloc = mel_alloc_heap());
        if (mel_io_status_failed(of.status))
        {
            emit("process", "stdout capture file open failed status=0x%x", of.status);
            s->step = 50;
            smoke_advance(self);
            return;
        }
        s->proc_out = of.value;
        static const char* const pargv[] = { "/bin/echo", "process-stdout-capture", NULL };
        Mel_Process_Spawn_Result sr = mel_process_spawn(.argv = pargv, .argc = 2, .stdout_cfg = { .disposition = MEL_PROCESS_STDIO_REDIRECT, .redirect = s->proc_out }, .vat = s->vat, .alloc = mel_alloc_heap());
        if (mel_process_status_failed(sr.status))
        {
            emit("process", "spawn failed status=0x%x", sr.status);
            mel_stream_destroy(s->proc_out);
            s->proc_out = NULL;
            s->step = 50;
            smoke_advance(self);
            return;
        }
        s->proc = sr.value;
        s->step = 40;
        smoke_chain(s, mel_process_wait(s->proc));
        return;
    }
    case 40:
    {
        const Mel_Process_Exit* ex = mel_process_wait_future_result(s->pending);
        char                    out[128] = { 0 };
        Mel_IO_Result           rd = mel_stream_read_sync(s->proc_out, out, sizeof out - 1, 0);
        usize                   n = rd.bytes_transferred;
        for (usize i = 0; i < n; i++)
            if (out[i] == '\n')
                out[i] = 0;
        emit("process", "spawned /bin/echo exit=%d captured=%zuB stdout=\"%s\" status=0x%x (file redirect; pipe capture needs fd wakeables the ui waiter lacks)", ex->exit_code, n, out, ex->status);
        mel_process_wait_future_release(s->pending);
        mel_process_destroy(s->proc);
        s->proc = NULL;
        mel_stream_destroy(s->proc_out);
        s->proc_out = NULL;
        remove(s->proc_out_path);
        s->step = 50;
        smoke_advance(self);
        return;
    }
    case 50:
    {
        emit("dialog", "backend available=%d (open-file picker suppressed in --smoke)", (int)mel_dialog_available());
        emit("shell", "backend available=%d (URL open suppressed in --smoke)", (int)mel_shell_available());
        s->step = 51;
        if (mel_clip_available())
        {
            smoke_chain(s, mel_clip_write_text(S8("melody-showcase clipboard probe"), .exec = s->exec));
            return;
        }
        emit("clipboard", "clipboard unavailable on host — honest-absent");
        s->step = 99;
        smoke_finish(s);
        return;
    }
    case 51:
    {
        Mel_Clip_Status ws = mel_clip_future_status(s->pending);
        mel_clip_future_free(s->pending);
        if (mel_clip_failed(ws))
        {
            emit("clipboard", "write failed status=0x%x", ws);
            s->step = 99;
            smoke_finish(s);
            return;
        }
        s->step = 52;
        smoke_chain(s, mel_clip_read_text(.exec = s->exec));
        return;
    }
    case 52:
    {
        str8            txt = mel_clip_future_text(s->pending);
        Mel_Clip_Status rs = mel_clip_future_status(s->pending);
        emit("clipboard", "copied then read back \"%.*s\" status=0x%x seq=%llu", (int)txt.len, (const char*)txt.data, rs, (unsigned long long)mel_clip_sequence());
        mel_clip_future_free(s->pending);
        s->step = 99;
        smoke_finish(s);
        return;
    }
    default:
        smoke_finish(s);
        return;
    }
}

static void smoke_kick(Mel_Task* task)
{
    (void)task;
    Smoke* s = &g_smoke;

    sync_modules(s);

    s->fs = mel_fs_create(.vat = s->vat, .alloc = mel_alloc_heap());

    Mel_Fs_Path_Result base = mel_fs_folder(MEL_FS_FOLDER_TEMP, mel_alloc_heap());
    snprintf(s->storage_root, sizeof s->storage_root, "%.*s/melody_showcase_store_%llu", (int)base.value.len, (const char*)base.value.data, (unsigned long long)(mel_nanos_since_unspecified_epoch() & 0xffffff));
    s->storage = mel_storage_open_fs(.root = str8_from_cstr(s->storage_root), .vat = s->vat, .alloc = mel_alloc_heap());

    mel_dialog_init(mel_alloc_heap(), s->vat);
    mel_shell_init(mel_alloc_heap());
    mel_clip_init(mel_alloc_heap(), s->vat);

    s->step = 0;
    Mel_Task* self = &s->cont;
    mel_task_init(self, smoke_advance);
    smoke_advance(self);
}

void showcase_smoke_setup(Mel_Vat* root)
{
    memset(&g_smoke, 0, sizeof g_smoke);
    g_smoke.vat = root;
    g_smoke.exec = mel_vat_executor(root);
    mel_task_init(&g_smoke.kick, smoke_kick);
    mel_vat_post(root, &g_smoke.kick);
}
