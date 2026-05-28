#include <emscripten.h>

#include <string.h>

#include "../display_backend.h"

u32 mel_display__enumerate(const Mel_Alloc* alloc, Mel_Display_Raw* out, u32 cap)
{
    (void)alloc;
    if (cap == 0) return 0;

    Mel_Display_Raw* r = &out[0];
    memset(r, 0, sizeof *r);
    r->stable_id = 0;

    Mel_Display_Descriptor* d = &r->desc;

    d->connector = MEL_DISPLAY_CONNECTOR_UNKNOWN;

    double dpr = EM_ASM_DOUBLE({ return window.devicePixelRatio || 1.0; });
    int w = EM_ASM_INT({ return (screen.width  * (window.devicePixelRatio || 1.0)) | 0; });
    int h = EM_ASM_INT({ return (screen.height * (window.devicePixelRatio || 1.0)) | 0; });
    d->native_resolution.width_px  = (u32)(w > 0 ? w : 0);
    d->native_resolution.height_px = (u32)(h > 0 ? h : 0);
    d->scale_factor = (f32)dpr;

    int hz = EM_ASM_INT({ return (screen.refreshRate || 0) | 0; });
    d->refresh_modes[0] = (Mel_Display_Mode){
        .width_px    = d->native_resolution.width_px,
        .height_px   = d->native_resolution.height_px,
        .refresh_mhz = (u32)(hz > 0 ? hz * 1000 : 0),
        .interlaced  = false,
    };
    d->refresh_mode_count = 1;

    Mel_Display_Hdr* hdr = &d->hdr;
    hdr->has_luminance = false;
    hdr->has_edr       = false;
    hdr->active        = EM_ASM_INT({
        return (window.matchMedia && window.matchMedia('(dynamic-range: high)').matches) ? 1 : 0;
    }) != 0;

    u32 n = 0;
    hdr->supported_color_spaces[n++] = MEL_COLOR_SPACE_SRGB;
    if (EM_ASM_INT({ return (window.matchMedia && window.matchMedia('(color-gamut: p3)').matches) ? 1 : 0; }))
        hdr->supported_color_spaces[n++] = MEL_COLOR_SPACE_DISPLAY_P3;
    if (EM_ASM_INT({ return (window.matchMedia && window.matchMedia('(color-gamut: rec2020)').matches) ? 1 : 0; }))
        hdr->supported_color_spaces[n++] = MEL_COLOR_SPACE_REC_2020;
    hdr->supported_color_space_count = n;

    hdr->mastering_primaries_support = MEL_DISPLAY_MASTERING_NONE;
    hdr->tone_mapping_owner          = MEL_DISPLAY_TONEMAP_COMPOSITOR;

    d->state           = MEL_DISPLAY_STATE_ACTIVE;
    d->native_handle   = (Mel_Display_Native_Handle){ .kind = MEL_DISPLAY_NATIVE_NONE };

    return 1;
}
