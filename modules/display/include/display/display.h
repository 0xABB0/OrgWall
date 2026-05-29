#pragma once

#include <core/types.h>
#include <reflect/enum.h>
#include <allocator/allocator.fwd.h>
#include <collection.slotmap/slotmap.fwd.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MEL_DISPLAY_NAME_CAP    128
#define MEL_DISPLAY_MAX_MODES   64

typedef enum {
    MEL_COLOR_SPACE_SRGB         MEL_STR("sRGB") = 0,
    MEL_COLOR_SPACE_DISPLAY_P3   MEL_STR("Display-P3"),
    MEL_COLOR_SPACE_REC_709      MEL_STR("Rec.709"),
    MEL_COLOR_SPACE_REC_2020     MEL_STR("Rec.2020"),
    MEL_COLOR_SPACE_SCRGB_LINEAR MEL_STR("scRGB-lin"),
    MEL_COLOR_SPACE_HDR10_PQ     MEL_STR("HDR10-PQ"),
    MEL_COLOR_SPACE_HLG,
    MEL_COLOR_SPACE_COUNT        MEL_SKIP,
} Mel_Color_Space;
MEL_ENUM_TO_STRING(Mel_Color_Space);

typedef struct {
    const u8* data;
    usize     size;
} Mel_Color_Icc_Profile;

typedef enum {
    MEL_DISPLAY_CONNECTOR_UNKNOWN     MEL_SKIP = 0,
    MEL_DISPLAY_CONNECTOR_INTERNAL    MEL_STR("Internal"),
    MEL_DISPLAY_CONNECTOR_HDMI,
    MEL_DISPLAY_CONNECTOR_DISPLAYPORT MEL_STR("DisplayPort"),
    MEL_DISPLAY_CONNECTOR_USB_C       MEL_STR("USB-C"),
    MEL_DISPLAY_CONNECTOR_VGA,
    MEL_DISPLAY_CONNECTOR_VIRTUAL     MEL_STR("Virtual"),
} Mel_Display_Connector;
MEL_ENUM_TO_STRING_DEFAULT(Mel_Display_Connector, "Unknown");

typedef enum {
    MEL_DISPLAY_STATE_ACTIVE       MEL_STR("Active") = 0,
    MEL_DISPLAY_STATE_MIRRORED     MEL_STR("Mirrored"),
    MEL_DISPLAY_STATE_DISCONNECTED MEL_STR("Disconnected"),
    MEL_DISPLAY_STATE_POWERED_OFF  MEL_STR("PoweredOff"),
    MEL_DISPLAY_STATE_DIMMED       MEL_STR("Dimmed"),
    MEL_DISPLAY_STATE_IDLE         MEL_STR("Idle"),
} Mel_Display_State;
MEL_ENUM_TO_STRING(Mel_Display_State);

typedef enum {
    MEL_DISPLAY_MASTERING_NONE    MEL_SKIP = 0,
    MEL_DISPLAY_MASTERING_STATIC  MEL_STR("Static"),
    MEL_DISPLAY_MASTERING_DYNAMIC MEL_STR("Dynamic"),
} Mel_Display_Mastering;
MEL_ENUM_TO_STRING_DEFAULT(Mel_Display_Mastering, "None");

typedef enum {
    MEL_DISPLAY_TONEMAP_DISPLAY     MEL_STR("Display") = 0,
    MEL_DISPLAY_TONEMAP_COMPOSITOR  MEL_STR("Compositor"),
    MEL_DISPLAY_TONEMAP_APPLICATION MEL_STR("Application"),
} Mel_Display_Tonemap;
MEL_ENUM_TO_STRING(Mel_Display_Tonemap);

typedef enum {
    MEL_DISPLAY_NATIVE_NONE            MEL_SKIP = 0,
    MEL_DISPLAY_NATIVE_NSSCREEN        MEL_STR("NSScreen*"),
    MEL_DISPLAY_NATIVE_UISCREEN        MEL_STR("UIScreen*"),
    MEL_DISPLAY_NATIVE_DXGI_OUTPUT6    MEL_STR("IDXGIOutput6*"),
    MEL_DISPLAY_NATIVE_VK_DISPLAY_KHR  MEL_STR("VkDisplayKHR"),
    MEL_DISPLAY_NATIVE_ANDROID_DISPLAY MEL_STR("Display(JNI)"),
    MEL_DISPLAY_NATIVE_WL_OUTPUT       MEL_STR("wl_output*"),
    MEL_DISPLAY_NATIVE_X11_OUTPUT      MEL_STR("RROutput"),
    MEL_DISPLAY_NATIVE_LOST            MEL_STR("Lost"),
} Mel_Display_Native_Kind;
MEL_ENUM_TO_STRING_DEFAULT(Mel_Display_Native_Kind, "None");

typedef struct {
    Mel_Display_Native_Kind kind;
    void* ptr;
    u64   id;
} Mel_Display_Native_Handle;

typedef struct { u32 width_px, height_px; } Mel_Display_Extent;

typedef struct {
    u32  width_px, height_px;
    u32  refresh_mhz;
    bool interlaced;
} Mel_Display_Mode;

typedef struct {
    bool has_luminance;
    f32  peak_luminance_nits, avg_luminance_nits, min_luminance_nits;

    bool has_edr;
    f32  edr_reference, edr_max_potential, edr_max_now;

    Mel_Color_Space supported_color_spaces[MEL_COLOR_SPACE_COUNT];
    u32             supported_color_space_count;

    Mel_Display_Mastering mastering_primaries_support;
    Mel_Display_Tonemap   tone_mapping_owner;
    bool                           active;
} Mel_Display_Hdr;

typedef struct {
    char                           name[MEL_DISPLAY_NAME_CAP];
    Mel_Display_Connector connector;
    Mel_Display_Extent    native_resolution;

    bool has_physical_size;
    f32  physical_width_mm, physical_height_mm;

    Mel_Display_Mode refresh_modes[MEL_DISPLAY_MAX_MODES];
    u32                       refresh_mode_count;

    bool has_vrr;
    u32  vrr_min_mhz, vrr_max_mhz;

    Mel_Display_Hdr hdr;

    Mel_Color_Icc_Profile icc_profile;

    Mel_Display_State state;

    bool has_position;
    i32  position_virtual_x, position_virtual_y;

    f32 scale_factor;

    Mel_Display_Native_Handle native_handle;
} Mel_Display_Descriptor;

typedef struct {
    Mel_SlotMap_Handle h;
} Mel_Display;

#define MEL_DISPLAY_NULL ((Mel_Display){0})

typedef enum {
    MEL_DISPLAY_STATUS_OK = 0,
    MEL_DISPLAY_STATUS_INVALID_HANDLE,
} Mel_Display_Status;

typedef struct {
    Mel_Display_Descriptor value;
    Mel_Display_Status     status;
} Mel_Display_Describe_Result;

enum {
    MEL_DISPLAY_FIELD_RESOLUTION = 1u << 0,
    MEL_DISPLAY_FIELD_REFRESH    = 1u << 1,
    MEL_DISPLAY_FIELD_VRR        = 1u << 2,
    MEL_DISPLAY_FIELD_HDR        = 1u << 3,
    MEL_DISPLAY_FIELD_ICC        = 1u << 4,
    MEL_DISPLAY_FIELD_SCALE      = 1u << 5,
    MEL_DISPLAY_FIELD_POSITION   = 1u << 6,
    MEL_DISPLAY_FIELD_STATE      = 1u << 7,
};

typedef enum {
    MEL_DISPLAY_EVENT_ADDED = 0,
    MEL_DISPLAY_EVENT_REMOVED,
    MEL_DISPLAY_EVENT_CONFIGURATION_CHANGED,
    MEL_DISPLAY_EVENT_POWER_STATE_CHANGED,
} Mel_Display_Event_Kind;

typedef struct {
    Mel_Display_Event_Kind kind;
    Mel_Display            display;
    u32                             changed_fields;
} Mel_Display_Event;

void mel_display_init(const Mel_Alloc* alloc);
void mel_display_shutdown(void);

u32  mel_display_refresh(void);
u32  mel_display_count(void);
u32  mel_display_list(Mel_Display* out, u32 cap);

Mel_Display_Describe_Result mel_display_describe(Mel_Display d);
bool                                 mel_display_alive(Mel_Display d);
bool                                 mel_display_equal(Mel_Display a, Mel_Display b);
Mel_Display_Native_Handle   mel_display_native_handle(Mel_Display d);

u32 mel_display_poll_events(Mel_Display_Event* out, u32 cap);

#ifdef __cplusplus
}
#endif
