#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

#include <allocator/allocator.h>

#include <display/macos/macos.h>

#include "../display_backend.h"

enum { MEL_MACOS_INTERLACED_FLAG = 0x00000200u };

static u32 refresh_hz_to_mhz(double hz)
{
    if (hz <= 0.0) return 0;
    return (u32)(hz * 1000.0 + 0.5);
}

static Mel_Color_Icc_Profile icc_copy(const Mel_Alloc* alloc, NSColorSpace* cs)
{
    Mel_Color_Icc_Profile out = {0};
    NSData* data = cs.ICCProfileData;
    if (!data || data.length == 0) return out;
    u8* buf = mel_alloc(alloc, data.length);
    if (!buf) return out;
    memcpy(buf, data.bytes, data.length);
    out.data = buf;
    out.size = data.length;
    return out;
}

static void fill_modes(CGDirectDisplayID id, Mel_Display_Descriptor* d)
{
    d->refresh_mode_count = 0;
    d->native_resolution  = (Mel_Display_Extent){0};

    CFArrayRef modes = CGDisplayCopyAllDisplayModes(id, NULL);
    if (!modes) {
        d->native_resolution.width_px  = (u32)CGDisplayPixelsWide(id);
        d->native_resolution.height_px = (u32)CGDisplayPixelsHigh(id);
        return;
    }

    CFIndex count = CFArrayGetCount(modes);
    u64 best_area = 0;
    for (CFIndex i = 0; i < count; i++) {
        CGDisplayModeRef m = (CGDisplayModeRef)CFArrayGetValueAtIndex(modes, i);
        u32 w = (u32)CGDisplayModeGetPixelWidth(m);
        u32 h = (u32)CGDisplayModeGetPixelHeight(m);

        if ((u64)w * h > best_area) {
            best_area = (u64)w * h;
            d->native_resolution.width_px  = w;
            d->native_resolution.height_px = h;
        }

        if (d->refresh_mode_count < MEL_DISPLAY_MAX_MODES) {
            u32 flags = CGDisplayModeGetIOFlags(m);
            d->refresh_modes[d->refresh_mode_count++] = (Mel_Display_Mode){
                .width_px    = w,
                .height_px   = h,
                .refresh_mhz = refresh_hz_to_mhz(CGDisplayModeGetRefreshRate(m)),
                .interlaced  = (flags & MEL_MACOS_INTERLACED_FLAG) != 0,
            };
        }
    }
    CFRelease(modes);
}

static void fill_hdr(NSScreen* screen, Mel_Display_Descriptor* d)
{
    Mel_Display_Hdr* hdr = &d->hdr;

    hdr->has_luminance = false;

    hdr->edr_max_now       = (f32)screen.maximumExtendedDynamicRangeColorComponentValue;
    hdr->edr_max_potential = (f32)screen.maximumPotentialExtendedDynamicRangeColorComponentValue;
    hdr->edr_reference     = (f32)screen.maximumReferenceExtendedDynamicRangeColorComponentValue;
    hdr->has_edr           = hdr->edr_max_potential > 1.0f;
    hdr->active            = hdr->edr_max_now > 1.0f;

    u32 n = 0;
    hdr->supported_color_spaces[n++] = MEL_COLOR_SPACE_SRGB;
    if ([screen canRepresentDisplayGamut:NSDisplayGamutP3])
        hdr->supported_color_spaces[n++] = MEL_COLOR_SPACE_DISPLAY_P3;
    hdr->supported_color_space_count = n;

    hdr->mastering_primaries_support = hdr->has_edr ? MEL_DISPLAY_MASTERING_STATIC
                                                    : MEL_DISPLAY_MASTERING_NONE;
    hdr->tone_mapping_owner = MEL_DISPLAY_TONEMAP_COMPOSITOR;
}

static Mel_Display_State display_state(CGDirectDisplayID id)
{
    if (!CGDisplayIsActive(id))     return MEL_DISPLAY_STATE_POWERED_OFF;
    if (CGDisplayIsInMirrorSet(id)) return MEL_DISPLAY_STATE_MIRRORED;
    return MEL_DISPLAY_STATE_ACTIVE;
}

u32 mel_display__enumerate(const Mel_Alloc* alloc, Mel_Display_Raw* out, u32 cap)
{
    NSArray<NSScreen*>* screens = NSScreen.screens;
    u32 n = 0;

    for (NSScreen* screen in screens) {
        if (n >= cap) break;

        NSNumber* num = screen.deviceDescription[@"NSScreenNumber"];
        if (!num) continue;
        CGDirectDisplayID id = (CGDirectDisplayID)num.unsignedIntValue;

        Mel_Display_Raw* r = &out[n++];
        memset(r, 0, sizeof *r);
        r->stable_id = (u64)id;

        Mel_Display_Descriptor* d = &r->desc;

        const char* name = screen.localizedName.UTF8String;
        if (name) {
            strncpy(d->name, name, MEL_DISPLAY_NAME_CAP - 1);
            d->name[MEL_DISPLAY_NAME_CAP - 1] = '\0';
        }

        d->connector = CGDisplayIsBuiltin(id) ? MEL_DISPLAY_CONNECTOR_INTERNAL
                                              : MEL_DISPLAY_CONNECTOR_UNKNOWN;

        fill_modes(id, d);

        CGSize mm = CGDisplayScreenSize(id);
        if (mm.width > 0.0 && mm.height > 0.0) {
            d->has_physical_size  = true;
            d->physical_width_mm  = (f32)mm.width;
            d->physical_height_mm = (f32)mm.height;
        }

        d->has_vrr = false;

        fill_hdr(screen, d);

        d->icc_profile = icc_copy(alloc, screen.colorSpace);

        d->state = display_state(id);

        NSRect frame = screen.frame;
        d->has_position       = true;
        d->position_virtual_x = (i32)frame.origin.x;
        d->position_virtual_y = (i32)frame.origin.y;

        d->scale_factor = (f32)screen.backingScaleFactor;
    }

    return n;
}

CGDirectDisplayID mel_display_macos_display_id(Mel_Display d)
{
    u64 id;
    return mel_display__stable_id(d, &id) ? (CGDirectDisplayID)id : kCGNullDirectDisplay;
}

NSScreen* mel_display_macos_screen(Mel_Display d)
{
    u64 id;
    if (!mel_display__stable_id(d, &id)) return nil;
    for (NSScreen* screen in NSScreen.screens) {
        NSNumber* num = screen.deviceDescription[@"NSScreenNumber"];
        if (num && (u64)num.unsignedIntValue == id) return screen;
    }
    return nil;
}
