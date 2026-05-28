#include <display/display.h>

#include <stdio.h>

static const char* connector_str(Mel_Display_Connector c) {
    switch (c) {
        case MEL_DISPLAY_CONNECTOR_INTERNAL:    return "Internal";
        case MEL_DISPLAY_CONNECTOR_HDMI:        return "HDMI";
        case MEL_DISPLAY_CONNECTOR_DISPLAYPORT: return "DisplayPort";
        case MEL_DISPLAY_CONNECTOR_USB_C:       return "USB-C";
        case MEL_DISPLAY_CONNECTOR_VGA:         return "VGA";
        case MEL_DISPLAY_CONNECTOR_VIRTUAL:     return "Virtual";
        default:                                         return "Unknown";
    }
}

static const char* state_str(Mel_Display_State s) {
    switch (s) {
        case MEL_DISPLAY_STATE_ACTIVE:       return "Active";
        case MEL_DISPLAY_STATE_MIRRORED:     return "Mirrored";
        case MEL_DISPLAY_STATE_DISCONNECTED: return "Disconnected";
        case MEL_DISPLAY_STATE_POWERED_OFF:  return "PoweredOff";
        case MEL_DISPLAY_STATE_DIMMED:       return "Dimmed";
        case MEL_DISPLAY_STATE_IDLE:         return "Idle";
        default:                                      return "?";
    }
}

static const char* color_space_str(Mel_Color_Space cs) {
    switch (cs) {
        case MEL_COLOR_SPACE_SRGB:         return "sRGB";
        case MEL_COLOR_SPACE_DISPLAY_P3:   return "Display-P3";
        case MEL_COLOR_SPACE_REC_709:      return "Rec.709";
        case MEL_COLOR_SPACE_REC_2020:     return "Rec.2020";
        case MEL_COLOR_SPACE_SCRGB_LINEAR: return "scRGB-linear";
        case MEL_COLOR_SPACE_HDR10_PQ:     return "HDR10-PQ";
        case MEL_COLOR_SPACE_HLG:          return "HLG";
        default:                           return "?";
    }
}

int main(void) {
    mel_display_init(NULL);

    u32 count = mel_display_count();
    printf("display: %u display(s)\n", count);

    Mel_Display handles[16];
    u32 n = mel_display_list(handles, 16);

    for (u32 i = 0; i < n && i < 16; i++) {
        Mel_Display_Describe_Result r = mel_display_describe(handles[i]);
        if (r.status != MEL_DISPLAY_STATUS_OK) {
            printf("[%u] describe failed: status=%d\n", i, (int)r.status);
            continue;
        }
        const Mel_Display_Descriptor* d = &r.value;

        printf("\n[%u] \"%s\"\n", i, d->name[0] ? d->name : "(unnamed)");
        printf("    connector       : %s\n", connector_str(d->connector));
        printf("    state           : %s\n", state_str(d->state));
        printf("    native_res      : %ux%u px\n", d->native_resolution.width_px, d->native_resolution.height_px);
        printf("    scale_factor    : %.2f\n", (double)d->scale_factor);
        if (d->has_physical_size)
            printf("    physical_size   : %.0f x %.0f mm\n", (double)d->physical_width_mm, (double)d->physical_height_mm);
        else
            printf("    physical_size   : (unpublished)\n");
        if (d->has_position)
            printf("    position_virtual: (%d, %d)\n", d->position_virtual_x, d->position_virtual_y);

        printf("    refresh_modes   : %u\n", d->refresh_mode_count);
        u32 show = d->refresh_mode_count < 6 ? d->refresh_mode_count : 6;
        for (u32 m = 0; m < show; m++) {
            const Mel_Display_Mode* mode = &d->refresh_modes[m];
            printf("        %ux%u @ %u.%03u Hz%s\n",
                   mode->width_px, mode->height_px,
                   mode->refresh_mhz / 1000, mode->refresh_mhz % 1000,
                   mode->interlaced ? " (interlaced)" : "");
        }
        if (d->refresh_mode_count > show) printf("        ... (%u more)\n", d->refresh_mode_count - show);

        printf("    hdr.edr         : now=%.2f potential=%.2f reference=%.2f%s\n",
               (double)d->hdr.edr_max_now, (double)d->hdr.edr_max_potential, (double)d->hdr.edr_reference,
               d->hdr.has_edr ? "" : " (no EDR)");
        printf("    hdr.luminance   : %s\n", d->hdr.has_luminance ? "published" : "(unpublished on macOS)");
        printf("    color_spaces    : ");
        for (u32 c = 0; c < d->hdr.supported_color_space_count; c++)
            printf("%s%s", c ? ", " : "", color_space_str(d->hdr.supported_color_spaces[c]));
        printf("\n");
        printf("    icc_profile     : %zu bytes\n", d->icc_profile.size);

        Mel_Display_Native_Handle nh = mel_display_native_handle(handles[i]);
        printf("    native_handle   : kind=%d ptr=%p\n", (int)nh.kind, nh.ptr);
    }

    mel_display_shutdown();
    return 0;
}
