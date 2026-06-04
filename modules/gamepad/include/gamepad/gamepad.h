#pragma once

#include <gamepad/joystick.h>
#include <gamepad/protocol.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Gamepad_Db Mel_Gamepad_Db;

typedef struct
{
    str8 platform_filter;
} Mel_Gamepad_Db_Opt;

Mel_Gamepad_Db* mel_gamepad_db_create_opt(const Mel_Alloc* alloc, Mel_Gamepad_Db_Opt opt);
#define mel_gamepad_db_create(alloc, ...) mel_gamepad_db_create_opt((alloc), (Mel_Gamepad_Db_Opt){ __VA_ARGS__ })

void mel_gamepad_db_destroy(Mel_Gamepad_Db* db);

u32  mel_gamepad_db_load_string(Mel_Gamepad_Db* db, str8 text);
bool mel_gamepad_db_load_line(Mel_Gamepad_Db* db, str8 line);
u32  mel_gamepad_db_count(const Mel_Gamepad_Db* db);

u32 mel_gamepad_db_load_bundled(Mel_Gamepad_Db* db);

void mel_gamepad_set_db(Mel_Gamepad_Db* db);

typedef enum
{
    MEL_GAMEPAD_BIND_NONE MEL_SKIP = 0,
    MEL_GAMEPAD_BIND_BUTTON MEL_STR("Button"),
    MEL_GAMEPAD_BIND_AXIS   MEL_STR("Axis"),
    MEL_GAMEPAD_BIND_HAT    MEL_STR("Hat"),
} Mel_Gamepad_Bind_Kind;

typedef struct
{
    Mel_Gamepad_Bind_Kind kind;
    u32                   index;
    u8                    hat_mask;
    i8                    axis_sign;
    bool                  invert;
} Mel_Gamepad_Binding;

bool mel_gamepad_supported(Mel_Joystick j);

bool mel_gamepad_button_binding(Mel_Joystick j, Mel_Gamepad_Button button, Mel_Gamepad_Binding* out);
bool mel_gamepad_axis_binding(Mel_Joystick j, Mel_Gamepad_Axis axis, Mel_Gamepad_Binding* out);

typedef struct
{
    bool down[MEL_GAMEPAD_BUTTON_COUNT];
    f32  axis[MEL_GAMEPAD_AXIS_COUNT];
    bool valid;
} Mel_Gamepad_Frame;

Mel_Gamepad_Frame mel_gamepad_read(Mel_Joystick j);

typedef enum
{
    MEL_GAMEPAD_FACE_LABELS_AB MEL_STR("AB") = 0,
    MEL_GAMEPAD_FACE_LABELS_SONY MEL_STR("Sony"),
    MEL_GAMEPAD_FACE_LABELS_NINTENDO MEL_STR("Nintendo"),
} Mel_Gamepad_Face_Labels;
MEL_ENUM_TO_STRING(Mel_Gamepad_Face_Labels);

Mel_Gamepad_Face_Labels mel_gamepad_face_labels(Mel_Joystick j);

str8 mel_gamepad_button_label(Mel_Gamepad_Button button, Mel_Gamepad_Face_Labels labels);

#ifdef __cplusplus
}
#endif
