#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include <core/platform.h>
#include <app/app.h>
#include <gui/gui.h>
#include <display/display.h>

#if MEL_PLATFORM_OSX
#include "edr.h"
#define EDR_FORCE_LEVEL 6.0f
#endif

#define TICK_NS      ((i64)250000000)
#define EVLOG_CAP    16
#define EVLOG_LINE   112
#define LBL(x)       ((const char*)(x).data)

typedef struct {
    Mel_Reactor*        reactor;
    Mel_Reactor_Source* timer;
    Mel_Gui_Handle      canvas;
    char                evlog[EVLOG_CAP][EVLOG_LINE];
    u32                 evlog_count;
    u64                 tick;
    bool                edr_force;
} Inspector;

static Inspector g;

static void fields_str(u32 f, char* out, size_t cap) {
    out[0] = 0;
    struct { u32 bit; const char* n; } map[] = {
        { MEL_DISPLAY_FIELD_RESOLUTION, "RES" }, { MEL_DISPLAY_FIELD_REFRESH, "REFRESH" },
        { MEL_DISPLAY_FIELD_VRR, "VRR" },        { MEL_DISPLAY_FIELD_HDR, "HDR" },
        { MEL_DISPLAY_FIELD_ICC, "ICC" },        { MEL_DISPLAY_FIELD_SCALE, "SCALE" },
        { MEL_DISPLAY_FIELD_POSITION, "POS" },   { MEL_DISPLAY_FIELD_STATE, "STATE" },
    };
    for (u32 i = 0; i < sizeof map / sizeof map[0]; i++) {
        if (!(f & map[i].bit)) continue;
        if (out[0]) strncat(out, "|", cap - strlen(out) - 1);
        strncat(out, map[i].n, cap - strlen(out) - 1);
    }
    if (!out[0]) strncpy(out, "(none)", cap - 1);
}

static void evlog_push(const char* line) {
    if (g.evlog_count == EVLOG_CAP) {
        memmove(g.evlog[0], g.evlog[1], (EVLOG_CAP - 1) * EVLOG_LINE);
        g.evlog_count--;
    }
    snprintf(g.evlog[g.evlog_count++], EVLOG_LINE, "%s", line);
    printf("[event] %s\n", line); fflush(stdout);
}

static void drain_events(void) {
    Mel_Display_Event ev[32];
    u32 n = mel_display_poll_events(ev, 32);
    for (u32 i = 0; i < n; i++) {
        char line[EVLOG_LINE];
        switch (ev[i].kind) {
            case MEL_DISPLAY_EVENT_ADDED:
                snprintf(line, sizeof line, "t%llu added  idx=%u gen=%u",
                         (unsigned long long)g.tick, ev[i].display.h.index, ev[i].display.h.generation);
                break;
            case MEL_DISPLAY_EVENT_REMOVED:
                snprintf(line, sizeof line, "t%llu removed  idx=%u gen=%u",
                         (unsigned long long)g.tick, ev[i].display.h.index, ev[i].display.h.generation);
                break;
            case MEL_DISPLAY_EVENT_POWER_STATE_CHANGED:
                snprintf(line, sizeof line, "t%llu power_state_changed  idx=%u",
                         (unsigned long long)g.tick, ev[i].display.h.index);
                break;
            default: {
                char fields[64];
                fields_str(ev[i].changed_fields, fields, sizeof fields);
                snprintf(line, sizeof line, "t%llu config_changed  idx=%u  fields=%s",
                         (unsigned long long)g.tick, ev[i].display.h.index, fields);
                break;
            }
        }
        evlog_push(line);
    }
}

static bool tick(void* user) {
    (void)user;
    g.tick++;
#if MEL_PLATFORM_OSX
    if (g.edr_force) display_edr_set(true, EDR_FORCE_LEVEL);
#endif
    mel_display_refresh();
    drain_events();
    if (!mel_gui_handle_is_none(g.canvas)) mel_gui_invalidate(g.canvas);
    return true;
}

#if MEL_PLATFORM_OSX
static void edr_toggle_clicked(Mel_Gui_Handle h, void* user) {
    (void)h; (void)user;
    g.edr_force = !g.edr_force;
    display_edr_set(g.edr_force, EDR_FORCE_LEVEL);
}
#endif

typedef struct { Mel_Painter* p; f32 x, y; } Pen;
static void line(Pen* pen, Mel_Color col, const char* fmt, ...) {
    char buf[256];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    mel_painter_draw_text(pen->p, str8_from_cstr(buf), mel_vec2(pen->x, pen->y), col, 12.0f);
    pen->y += 15.0f;
}

static void canvas_paint(Mel_Gui_Handle h, Mel_Painter* p, i32 w, i32 height, void* user) {
    (void)h; (void)user;
    Mel_Color bg   = mel_rgb(24, 28, 34);
    Mel_Color head = mel_rgb(120, 200, 255);
    Mel_Color key  = mel_rgb(170, 185, 205);
    Mel_Color hot  = mel_rgb(255, 200, 110);
    Mel_Color evc  = mel_rgb(150, 230, 160);

    mel_painter_clear(p, bg);

    Mel_Display handles[8];
    u32 n = mel_display_list(handles, 8);
    u32 total = mel_display_count();

    Pen pen = { .p = p, .x = 12, .y = 10 };
    line(&pen, head, "display inspector  —  tick %llu  —  %u display(s)  [live, %.0f ms]  —  EDR force: %s",
         (unsigned long long)g.tick, total, (double)TICK_NS / 1e6,
         g.edr_force ? "ON" : "off");
    pen.y += 4;

    for (u32 i = 0; i < n; i++) {
        Mel_Display_Describe_Result r = mel_display_describe(handles[i]);
        if (r.status != MEL_DISPLAY_STATUS_OK) { line(&pen, key, "[%u] <invalid>", i); continue; }
        const Mel_Display_Descriptor* d = &r.value;
        const Mel_Display_Hdr* hd = &d->hdr;

        line(&pen, head, "[%u] \"%s\"   handle{idx=%u,gen=%u}", i,
             d->name[0] ? d->name : "(unnamed)", handles[i].h.index, handles[i].h.generation);
        line(&pen, key,  "    connector=%s  state=%s  native=%ux%u px  scale=%.3f  pos=(%d,%d)%s",
             LBL(Mel_Display_Connector_to_string(d->connector)), LBL(Mel_Display_State_to_string(d->state)),
             d->native_resolution.width_px, d->native_resolution.height_px,
             (double)d->scale_factor, d->position_virtual_x, d->position_virtual_y,
             d->has_position ? "" : " (no pos)");
        line(&pen, key,  "    physical=%s%.0fx%.0f mm  icc=%zu B  native_handle=%s id=%llu",
             d->has_physical_size ? "" : "? ",
             (double)d->physical_width_mm, (double)d->physical_height_mm,
             d->icc_profile.size, LBL(Mel_Display_Native_Kind_to_string(d->native_handle.kind)),
             (unsigned long long)d->native_handle.id);

        line(&pen, hot,  "    EDR now=%.3f  potential=%.3f  reference=%.3f  has_edr=%d  hdr.active=%d",
             (double)hd->edr_max_now, (double)hd->edr_max_potential, (double)hd->edr_reference,
             hd->has_edr, hd->active);
        line(&pen, key,  "    luminance: %s peak=%.0f avg=%.0f min=%.3f nits   master=%s  tonemap=%s",
             hd->has_luminance ? "" : "(unpublished) ",
             (double)hd->peak_luminance_nits, (double)hd->avg_luminance_nits,
             (double)hd->min_luminance_nits, LBL(Mel_Display_Mastering_to_string(hd->mastering_primaries_support)),
             LBL(Mel_Display_Tonemap_to_string(hd->tone_mapping_owner)));

        char cs[160]; cs[0] = 0;
        for (u32 c = 0; c < hd->supported_color_space_count; c++) {
            if (cs[0]) strncat(cs, ", ", sizeof cs - strlen(cs) - 1);
            strncat(cs, LBL(Mel_Color_Space_to_string(hd->supported_color_spaces[c])), sizeof cs - strlen(cs) - 1);
        }
        line(&pen, key, "    color spaces: %s", cs);

        if (d->has_vrr) line(&pen, key, "    vrr: %u-%u mHz", d->vrr_min_mhz, d->vrr_max_mhz);

        line(&pen, key, "    refresh modes (%u):", d->refresh_mode_count);
        char row[240]; row[0] = 0; u32 per = 0;
        for (u32 m = 0; m < d->refresh_mode_count; m++) {
            const Mel_Display_Mode* md = &d->refresh_modes[m];
            char tok[48];
            snprintf(tok, sizeof tok, "%ux%u@%u.%03u%s", md->width_px, md->height_px,
                     md->refresh_mhz / 1000, md->refresh_mhz % 1000, md->interlaced ? "i" : "");
            if (per && (strlen(row) + strlen(tok) + 2 > 78 || per == 4)) {
                line(&pen, mel_rgb(120,135,155), "      %s", row); row[0] = 0; per = 0;
            }
            if (per) strncat(row, "  ", sizeof row - strlen(row) - 1);
            strncat(row, tok, sizeof row - strlen(row) - 1);
            per++;
        }
        if (row[0]) line(&pen, mel_rgb(120,135,155), "      %s", row);
        pen.y += 6;
    }

    f32 ey = (f32)height - 12 - (f32)(g.evlog_count + 1) * 15.0f;
    if (ey < pen.y) ey = pen.y;
    Pen ev = { .p = p, .x = 12, .y = ey };
    mel_painter_draw_line(p, mel_vec2(0, ey - 4), mel_vec2((f32)w, ey - 4), mel_rgb(60, 70, 82), 1.0f);
    line(&ev, evc, "event log (most recent %u):", g.evlog_count);
    for (u32 i = 0; i < g.evlog_count; i++) line(&ev, evc, "  %s", g.evlog[i]);
}

static void build_inspector(Mel_Gui_Handle frame, void* user) {
    (void)user;
    mel_gui_set_text(frame, S8("Display Inspector"));
    mel_gui_set_layout(frame, mel_column_layout(.spacing = 4, .margin = 4, .cross_align = MEL_ALIGN_STRETCH));
#if MEL_PLATFORM_OSX
    mel_button_create(frame, .text = S8("Toggle EDR headroom window (forces 'EDR now' > 1.0)"),
        .pointer.on_click = edr_toggle_clicked,
        .layoutable = { .preferred_h = 30 });
#endif
    g.canvas = mel_canvas_create(frame, .on_.on_paint = canvas_paint,
        .layoutable = { .preferred_w = 900, .preferred_h = 660, .weight = 1 });

    g.timer = mel_reactor_timer_new(TICK_NS, tick, NULL);
    if (g.timer) mel_reactor_source_attach(g.reactor, g.timer);
}

void mel_app_setup(Mel_Reactor* reactor) {
    memset(&g, 0, sizeof g);
    g.canvas = MEL_GUI_HANDLE_NONE;
    g.reactor = reactor;
    mel_gui_init(reactor);
    mel_display_init(NULL);
    mel_app_register_screen(S8("inspector"), build_inspector, NULL);
    mel_app_present(S8("inspector"));
}
