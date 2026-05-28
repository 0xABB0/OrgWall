#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

#include <string.h>

#include "../display_backend.h"

static Mel_Display_Connector connector_from_name(const char* name)
{
    if (strncmp(name, "eDP", 3) == 0 || strncmp(name, "LVDS", 4) == 0) return MEL_DISPLAY_CONNECTOR_INTERNAL;
    if (strncmp(name, "HDMI", 4) == 0)                                 return MEL_DISPLAY_CONNECTOR_HDMI;
    if (strncmp(name, "DP", 2) == 0 || strncmp(name, "DisplayPort", 11) == 0) return MEL_DISPLAY_CONNECTOR_DISPLAYPORT;
    if (strncmp(name, "VGA", 3) == 0)                                  return MEL_DISPLAY_CONNECTOR_VGA;
    return MEL_DISPLAY_CONNECTOR_UNKNOWN;
}

static u32 mode_refresh_mhz(const XRRModeInfo* m)
{
    double vtotal = (double)m->vTotal;
    if (m->modeFlags & RR_DoubleScan) vtotal *= 2.0;
    if (m->modeFlags & RR_Interlace)  vtotal /= 2.0;
    if (m->hTotal == 0 || vtotal == 0.0) return 0;
    double rate = (double)m->dotClock / ((double)m->hTotal * vtotal);
    return (u32)(rate * 1000.0 + 0.5);
}

static const XRRModeInfo* find_mode(const XRRScreenResources* res, RRMode id)
{
    for (int i = 0; i < res->nmode; i++)
        if (res->modes[i].id == id) return &res->modes[i];
    return NULL;
}

u32 mel_display__enumerate(const Mel_Alloc* alloc, Mel_Display_Raw* out, u32 cap)
{
    (void)alloc;
    Display* dpy = XOpenDisplay(NULL);
    if (!dpy) return 0;

    Window root = DefaultRootWindow(dpy);
    XRRScreenResources* res = XRRGetScreenResourcesCurrent(dpy, root);
    if (!res) { XCloseDisplay(dpy); return 0; }

    u32 n = 0;
    for (int o = 0; o < res->noutput && n < cap; o++) {
        XRROutputInfo* oi = XRRGetOutputInfo(dpy, res, res->outputs[o]);
        if (!oi) continue;
        if (oi->connection != RR_Connected || oi->crtc == 0) { XRRFreeOutputInfo(oi); continue; }

        Mel_Display_Raw* r = &out[n++];
        memset(r, 0, sizeof *r);
        r->stable_id = (u64)res->outputs[o];
        Mel_Display_Descriptor* d = &r->desc;

        size_t nlen = (size_t)oi->nameLen < MEL_DISPLAY_NAME_CAP - 1 ? (size_t)oi->nameLen
                                                                     : MEL_DISPLAY_NAME_CAP - 1;
        memcpy(d->name, oi->name, nlen);
        d->name[nlen] = 0;
        d->connector = connector_from_name(d->name);

        if (oi->mm_width > 0 && oi->mm_height > 0) {
            d->has_physical_size  = true;
            d->physical_width_mm  = (f32)oi->mm_width;
            d->physical_height_mm = (f32)oi->mm_height;
        }

        XRRCrtcInfo* ci = XRRGetCrtcInfo(dpy, res, oi->crtc);
        if (ci) {
            d->has_position       = true;
            d->position_virtual_x = ci->x;
            d->position_virtual_y = ci->y;
            d->native_resolution.width_px  = ci->width;
            d->native_resolution.height_px = ci->height;
            XRRFreeCrtcInfo(ci);
        }

        u64 best_area = 0;
        for (int mi = 0; mi < oi->nmode && d->refresh_mode_count < MEL_DISPLAY_MAX_MODES; mi++) {
            const XRRModeInfo* m = find_mode(res, oi->modes[mi]);
            if (!m) continue;
            d->refresh_modes[d->refresh_mode_count++] = (Mel_Display_Mode){
                .width_px    = m->width,
                .height_px   = m->height,
                .refresh_mhz = mode_refresh_mhz(m),
                .interlaced  = (m->modeFlags & RR_Interlace) != 0,
            };
            if ((u64)m->width * m->height > best_area) {
                best_area = (u64)m->width * m->height;
                if (d->native_resolution.width_px == 0) {
                    d->native_resolution.width_px  = m->width;
                    d->native_resolution.height_px = m->height;
                }
            }
        }

        d->scale_factor = 1.0f;
        d->state        = MEL_DISPLAY_STATE_ACTIVE;

        Mel_Display_Hdr* hdr = &d->hdr;
        hdr->has_luminance               = false;
        hdr->has_edr                     = false;
        hdr->supported_color_spaces[0]   = MEL_COLOR_SPACE_SRGB;
        hdr->supported_color_space_count = 1;
        hdr->mastering_primaries_support = MEL_DISPLAY_MASTERING_NONE;
        hdr->tone_mapping_owner          = MEL_DISPLAY_TONEMAP_APPLICATION;

        d->native_handle = (Mel_Display_Native_Handle){
            .kind = MEL_DISPLAY_NATIVE_X11_OUTPUT,
            .ptr  = NULL,
            .id   = (u64)res->outputs[o],
        };

        XRRFreeOutputInfo(oi);
    }

    XRRFreeScreenResources(res);
    XCloseDisplay(dpy);
    return n;
}
