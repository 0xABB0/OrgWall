#pragma once

#include "../gui_internal.h"

/* The native surface handle the gpu vulkan linux lowering expects. Its field
 * layout MUST match modules/gpu/src/vulkan/linux/surface.h's Mel_Gpu_Linux_Native
 * exactly: gpu receives this as an opaque void* (mel_gpu_view_surface) and casts
 * it to its own struct, reading the fields by offset. gui and gpu stay free of a
 * direct dependency (gui/controls/gpu_view.h), so the contract is the layout, not
 * a shared type. Wayland fields are present but unused by this XCB backend. */
typedef struct
{
    void* wl_display;
    void* wl_surface;

    void* xcb_connection;
    u32   xcb_window;
} Mel_Gui_Xcb_Native;

typedef u32 mel_xcb_window;
typedef u32 mel_xcb_colormap;
typedef u32 mel_xcb_visualid;
typedef u32 mel_xcb_atom;

typedef struct mel_xcb_connection mel_xcb_connection;
typedef struct mel_xcb_setup      mel_xcb_setup;

typedef struct
{
    u8  response_type;
    u8  pad0;
    u16 sequence;
    u32 pad[7];
    u32 full_sequence;
} mel_xcb_generic_event;

typedef struct
{
    u8             response_type;
    u8             format;
    u16            sequence;
    mel_xcb_window window;
    mel_xcb_atom   type;
    u32            data32[5];
} mel_xcb_client_message_event;

typedef struct
{
    u8             response_type;
    u8             pad0;
    u16            sequence;
    mel_xcb_window event;
    mel_xcb_window window;
    mel_xcb_window above_sibling;
    i16            x;
    i16            y;
    u16            width;
    u16            height;
    u16            border_width;
    u8             override_redirect;
    u8             pad1;
} mel_xcb_configure_notify_event;

typedef struct
{
    u8             response_type;
    u8             detail;
    u16            sequence;
    u32            time;
    mel_xcb_window root;
    mel_xcb_window event;
    mel_xcb_window child;
    i16            root_x;
    i16            root_y;
    i16            event_x;
    i16            event_y;
    u16            state;
    u8             same_screen;
    u8             pad0;
} mel_xcb_input_event;

typedef struct
{
    mel_xcb_window   root;
    mel_xcb_colormap default_colormap;
    u32              white_pixel;
    u32              black_pixel;
    u32              current_input_masks;
    u16              width_in_pixels;
    u16              height_in_pixels;
    u16              width_in_millimeters;
    u16              height_in_millimeters;
    u16              min_installed_maps;
    u16              max_installed_maps;
    mel_xcb_visualid root_visual;
    u8               backing_stores;
    u8               save_unders;
    u8               root_depth;
    u8               allowed_depths_len;
} mel_xcb_screen;

typedef struct
{
    mel_xcb_screen* data;
    int             rem;
    int             index;
} mel_xcb_screen_iterator;

typedef struct
{
    unsigned int sequence;
} mel_xcb_void_cookie;

typedef struct
{
    unsigned int sequence;
} mel_xcb_intern_atom_cookie;

typedef struct
{
    u8           response_type;
    u8           pad0;
    u16          sequence;
    u32          length;
    mel_xcb_atom atom;
} mel_xcb_intern_atom_reply;

typedef struct
{
    mel_xcb_connection* (*connect)(const char* displayname, int* screenp);
    int (*connection_has_error)(mel_xcb_connection*);
    void (*disconnect)(mel_xcb_connection*);
    int (*get_file_descriptor)(mel_xcb_connection*);
    int (*flush)(mel_xcb_connection*);
    u32 (*generate_id)(mel_xcb_connection*);
    const mel_xcb_setup* (*get_setup)(mel_xcb_connection*);
    mel_xcb_screen_iterator (*setup_roots_iterator)(const mel_xcb_setup*);
    void (*screen_next)(mel_xcb_screen_iterator*);
    mel_xcb_generic_event* (*poll_for_event)(mel_xcb_connection*);
    mel_xcb_void_cookie (*create_window)(mel_xcb_connection*, u8 depth, mel_xcb_window wid, mel_xcb_window parent, i16 x, i16 y, u16 width, u16 height, u16 border_width, u16 class_, mel_xcb_visualid visual, u32 value_mask,
                                         const void* value_list);
    mel_xcb_void_cookie (*destroy_window)(mel_xcb_connection*, mel_xcb_window);
    mel_xcb_void_cookie (*map_window)(mel_xcb_connection*, mel_xcb_window);
    mel_xcb_void_cookie (*unmap_window)(mel_xcb_connection*, mel_xcb_window);
    mel_xcb_void_cookie (*configure_window)(mel_xcb_connection*, mel_xcb_window, u16 value_mask, const void* value_list);
    mel_xcb_void_cookie (*change_property)(mel_xcb_connection*, u8 mode, mel_xcb_window, mel_xcb_atom property, mel_xcb_atom type, u8 format, u32 data_len, const void* data);
    mel_xcb_void_cookie (*change_window_attributes)(mel_xcb_connection*, mel_xcb_window, u32 value_mask, const void* value_list);
    mel_xcb_void_cookie (*set_input_focus)(mel_xcb_connection*, u8 revert_to, mel_xcb_window focus, u32 time);
    mel_xcb_intern_atom_cookie (*intern_atom)(mel_xcb_connection*, u8 only_if_exists, u16 name_len, const char* name);
    mel_xcb_intern_atom_reply* (*intern_atom_reply)(mel_xcb_connection*, mel_xcb_intern_atom_cookie, void* error);
} mel_xcb_api;

#define MEL_XCB_COPY_FROM_PARENT            0L
#define MEL_XCB_WINDOW_CLASS_INPUT_OUTPUT   1

#define MEL_XCB_CW_BACK_PIXEL               2u
#define MEL_XCB_CW_EVENT_MASK               2048u

#define MEL_XCB_CONFIG_WINDOW_X             1u
#define MEL_XCB_CONFIG_WINDOW_Y             2u
#define MEL_XCB_CONFIG_WINDOW_WIDTH         4u
#define MEL_XCB_CONFIG_WINDOW_HEIGHT        8u

#define MEL_XCB_EVENT_MASK_KEY_PRESS        1u
#define MEL_XCB_EVENT_MASK_KEY_RELEASE      2u
#define MEL_XCB_EVENT_MASK_BUTTON_PRESS     4u
#define MEL_XCB_EVENT_MASK_BUTTON_RELEASE   8u
#define MEL_XCB_EVENT_MASK_POINTER_MOTION   64u
#define MEL_XCB_EVENT_MASK_STRUCTURE_NOTIFY 131072u

#define MEL_XCB_PROP_MODE_REPLACE           0

#define MEL_XCB_ATOM_STRING                 31
#define MEL_XCB_ATOM_WM_NAME                39

#define MEL_XCB_INPUT_FOCUS_POINTER_ROOT    1

#define MEL_XCB_KEY_PRESS                   2
#define MEL_XCB_KEY_RELEASE                 3
#define MEL_XCB_BUTTON_PRESS                4
#define MEL_XCB_BUTTON_RELEASE              5
#define MEL_XCB_MOTION_NOTIFY               6
#define MEL_XCB_CONFIGURE_NOTIFY            22
#define MEL_XCB_CLIENT_MESSAGE              33

typedef struct
{
    void*               lib;
    mel_xcb_api         api;
    mel_xcb_connection* conn;
    mel_xcb_screen*     screen;
    mel_xcb_window      root;
    mel_xcb_visualid    visual;
    u8                  depth;
    u32                 black_pixel;
    mel_xcb_atom        wm_protocols;
    mel_xcb_atom        wm_delete_window;
    Mel_Reactor_Source* source;
    bool                ok;
} Mel_Xcb_State;

Mel_Xcb_State* mel_gui__xcb(void);

Mel_Gui_Handle mel_gui__xcb_handle_of_window(mel_xcb_window w);

mel_xcb_window mel_gui__xcb_parent_window(Mel_Gui_Node* n);

mel_xcb_window mel_gui__xcb_create_child(Mel_Gui_Node* n, u32 extra_event_mask);

void mel_gui__xcb_view_resized(mel_xcb_window window, i32 w, i32 h);
void mel_gui__xcb_view_pointer(mel_xcb_window window, u8 type, i32 x, i32 y);
void mel_gui__xcb_view_drop(mel_xcb_window window);
