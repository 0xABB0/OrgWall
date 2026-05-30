#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_6.h>

#include <string.h>

#include <display/win32/win32.h>

#include "../display_backend.h"

static u64 fnv1a_wide(const WCHAR* s)
{
    u64 h = 1469598103934665603ull;
    for (; *s; s++) { h ^= (u64)(u16)*s; h *= 1099511628211ull; }
    return h;
}

static u32 rational_to_mhz(DXGI_RATIONAL r)
{
    if (r.Denominator == 0) return 0;
    return (u32)(((u64)r.Numerator * 1000ull + r.Denominator / 2) / r.Denominator);
}

static bool is_hdr10_space(DXGI_COLOR_SPACE_TYPE cs)
{
    return cs == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
}

static void fill_modes(IDXGIOutput* output, Mel_Display_Descriptor* d)
{
    d->refresh_mode_count = 0;
    UINT num = 0;
    const DXGI_FORMAT fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (FAILED(IDXGIOutput_GetDisplayModeList(output, fmt, 0, &num, NULL)) || num == 0)
        return;

    DXGI_MODE_DESC* modes = (DXGI_MODE_DESC*)calloc(num, sizeof *modes);
    if (!modes) return;
    if (SUCCEEDED(IDXGIOutput_GetDisplayModeList(output, fmt, 0, &num, modes))) {
        for (UINT i = 0; i < num && d->refresh_mode_count < MEL_DISPLAY_MAX_MODES; i++) {
            bool interlaced = modes[i].ScanlineOrdering == DXGI_MODE_SCANLINE_ORDER_UPPER_FIELD_FIRST ||
                              modes[i].ScanlineOrdering == DXGI_MODE_SCANLINE_ORDER_LOWER_FIELD_FIRST;
            d->refresh_modes[d->refresh_mode_count++] = (Mel_Display_Mode){
                .width_px    = modes[i].Width,
                .height_px   = modes[i].Height,
                .refresh_mhz = rational_to_mhz(modes[i].RefreshRate),
                .interlaced  = interlaced,
            };
        }
    }
    free(modes);
}

static void fill_from_desc(const DXGI_OUTPUT_DESC1* desc, Mel_Display_Descriptor* d)
{
    WideCharToMultiByte(CP_UTF8, 0, desc->DeviceName, -1, d->name, MEL_DISPLAY_NAME_CAP - 1, NULL, NULL);

    d->connector = MEL_DISPLAY_CONNECTOR_UNKNOWN;

    LONG w = desc->DesktopCoordinates.right - desc->DesktopCoordinates.left;
    LONG h = desc->DesktopCoordinates.bottom - desc->DesktopCoordinates.top;
    d->native_resolution.width_px  = (u32)(w > 0 ? w : 0);
    d->native_resolution.height_px = (u32)(h > 0 ? h : 0);

    d->has_position       = true;
    d->position_virtual_x = (i32)desc->DesktopCoordinates.left;
    d->position_virtual_y = (i32)desc->DesktopCoordinates.top;

    d->scale_factor = 1.0f;

    d->state = desc->AttachedToDesktop ? MEL_DISPLAY_STATE_ACTIVE
                                       : MEL_DISPLAY_STATE_DISCONNECTED;

    Mel_Display_Hdr* hdr = &d->hdr;
    bool hdr_space = is_hdr10_space(desc->ColorSpace);

    hdr->has_luminance       = desc->MaxLuminance > 0.0f;
    hdr->peak_luminance_nits = desc->MaxLuminance;
    hdr->avg_luminance_nits  = desc->MaxFullFrameLuminance;
    hdr->min_luminance_nits  = desc->MinLuminance;
    hdr->has_edr             = false;

    u32 cs = 0;
    hdr->supported_color_spaces[cs++] = MEL_COLOR_SPACE_SRGB;
    if (hdr_space) {
        hdr->supported_color_spaces[cs++] = MEL_COLOR_SPACE_REC_2020;
        hdr->supported_color_spaces[cs++] = MEL_COLOR_SPACE_HDR10_PQ;
    }
    hdr->supported_color_space_count = cs;

    hdr->mastering_primaries_support = (hdr_space && desc->BitsPerColor >= 10)
        ? MEL_DISPLAY_MASTERING_STATIC : MEL_DISPLAY_MASTERING_NONE;
    hdr->tone_mapping_owner = MEL_DISPLAY_TONEMAP_DISPLAY;
    hdr->active             = hdr_space;
}

u32 mel_display__enumerate(const Mel_Alloc* alloc, Mel_Display_Raw* out, u32 cap)
{
    (void)alloc;
    IDXGIFactory1* factory = NULL;
    if (FAILED(CreateDXGIFactory1(&IID_IDXGIFactory1, (void**)&factory)) || !factory)
        return 0;

    u32 n = 0;
    for (UINT ai = 0; n < cap; ai++) {
        IDXGIAdapter1* adapter = NULL;
        if (IDXGIFactory1_EnumAdapters1(factory, ai, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        if (!adapter) break;

        for (UINT oi = 0; n < cap; oi++) {
            IDXGIOutput* output = NULL;
            if (IDXGIAdapter1_EnumOutputs(adapter, oi, &output) == DXGI_ERROR_NOT_FOUND) break;
            if (!output) break;

            IDXGIOutput6* out6 = NULL;
            if (SUCCEEDED(IDXGIOutput_QueryInterface(output, &IID_IDXGIOutput6, (void**)&out6)) && out6) {
                DXGI_OUTPUT_DESC1 desc;
                if (SUCCEEDED(IDXGIOutput6_GetDesc1(out6, &desc))) {
                    Mel_Display_Raw* r = &out[n++];
                    memset(r, 0, sizeof *r);
                    r->stable_id = fnv1a_wide(desc.DeviceName);
                    fill_from_desc(&desc, &r->desc);
                    fill_modes(output, &r->desc);
                }
                IDXGIOutput6_Release(out6);
            }
            IDXGIOutput_Release(output);
        }
        IDXGIAdapter1_Release(adapter);
    }

    IDXGIFactory1_Release(factory);
    return n;
}

const char* mel_display_win32_device_name(Mel_Display d)
{
    const Mel_Display_Descriptor* desc = mel_display__descriptor(d);
    return desc ? desc->name : "";
}
