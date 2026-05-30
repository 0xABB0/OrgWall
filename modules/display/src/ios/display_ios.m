#import <UIKit/UIKit.h>

#include <allocator/allocator.h>

#include <display/ios/ios.h>

#include "../display_backend.h"

static void fill_hdr(UIScreen* screen, Mel_Display_Descriptor* d)
{
    Mel_Display_Hdr* hdr = &d->hdr;
    hdr->has_luminance = false;

    if (@available(iOS 16.0, *)) {
        hdr->edr_max_now       = (f32)screen.currentEDRHeadroom;
        hdr->edr_max_potential = (f32)screen.potentialEDRHeadroom;
        hdr->edr_reference     = 0.0f;
        hdr->has_edr           = hdr->edr_max_potential > 1.0f;
        hdr->active            = hdr->edr_max_now > 1.0f;
    }

    u32 n = 0;
    hdr->supported_color_spaces[n++] = MEL_COLOR_SPACE_SRGB;
    if (screen.traitCollection.displayGamut == UIDisplayGamutP3)
        hdr->supported_color_spaces[n++] = MEL_COLOR_SPACE_DISPLAY_P3;
    hdr->supported_color_space_count = n;

    hdr->mastering_primaries_support = hdr->has_edr ? MEL_DISPLAY_MASTERING_STATIC
                                                    : MEL_DISPLAY_MASTERING_NONE;
    hdr->tone_mapping_owner = MEL_DISPLAY_TONEMAP_COMPOSITOR;
}

u32 mel_display__enumerate(const Mel_Alloc* alloc, Mel_Display_Raw* out, u32 cap)
{
    (void)alloc;
    NSArray<UIScreen*>* screens = UIScreen.screens;
    UIScreen* main = UIScreen.mainScreen;
    u32 n = 0;

    for (UIScreen* screen in screens) {
        if (n >= cap) break;

        Mel_Display_Raw* r = &out[n];
        memset(r, 0, sizeof *r);
        r->stable_id = (u64)(uintptr_t)screen;

        Mel_Display_Descriptor* d = &r->desc;

        d->connector = (screen == main) ? MEL_DISPLAY_CONNECTOR_INTERNAL
                                        : MEL_DISPLAY_CONNECTOR_UNKNOWN;

        CGRect px = screen.nativeBounds;
        d->native_resolution.width_px  = (u32)px.size.width;
        d->native_resolution.height_px = (u32)px.size.height;

        d->refresh_mode_count = 0;
        u32 fps = (u32)screen.maximumFramesPerSecond;
        for (UIScreenMode* mode in screen.availableModes) {
            if (d->refresh_mode_count >= MEL_DISPLAY_MAX_MODES) break;
            d->refresh_modes[d->refresh_mode_count++] = (Mel_Display_Mode){
                .width_px    = (u32)mode.size.width,
                .height_px   = (u32)mode.size.height,
                .refresh_mhz = fps * 1000u,
                .interlaced  = false,
            };
        }
        if (d->refresh_mode_count == 0) {
            d->refresh_modes[d->refresh_mode_count++] = (Mel_Display_Mode){
                .width_px    = d->native_resolution.width_px,
                .height_px   = d->native_resolution.height_px,
                .refresh_mhz = fps * 1000u,
                .interlaced  = false,
            };
        }

        fill_hdr(screen, d);

        d->state        = MEL_DISPLAY_STATE_ACTIVE;
        d->scale_factor = (f32)screen.nativeScale;
        n++;
    }

    return n;
}

UIScreen* mel_display_ios_screen(Mel_Display d)
{
    u64 id;
    if (!mel_display__stable_id(d, &id)) return nil;
    for (UIScreen* screen in UIScreen.screens)
        if ((u64)(uintptr_t)screen == id) return screen;
    return nil;
}
