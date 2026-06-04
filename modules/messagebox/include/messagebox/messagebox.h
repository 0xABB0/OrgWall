#pragma once

#include <core/types.h>
#include <core/compiler.h>
#include <string/str8.h>
#include <color/rgba8.h>
#include <window/window.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef u32 Mel_Msgbox_Status;

#define MEL_MSGBOX_SEVERITY_MASK 0x3u
#define MEL_MSGBOX_OK            0u
#define MEL_MSGBOX_WARNED        1u
#define MEL_MSGBOX_ERROR         2u

#define MEL_MSGBOX_RESULT_DISMISSED  (1u << 2)
#define MEL_MSGBOX_RESULT_NO_BACKEND (1u << 3)
#define MEL_MSGBOX_RESULT_DEFAULTED  (1u << 4)

#define MEL_MSGBOX_WARN_BUTTONS_COLLAPSED (1u << 8)
#define MEL_MSGBOX_WARN_COLOR_DROPPED     (1u << 9)
#define MEL_MSGBOX_WARN_RTL_DROPPED       (1u << 10)
#define MEL_MSGBOX_WARN_PARENT_DROPPED    (1u << 11)
#define MEL_MSGBOX_WARN_TITLE_SYNTHESIZED (1u << 12)

static inline bool mel_msgbox_failed(Mel_Msgbox_Status s) { return (s & MEL_MSGBOX_SEVERITY_MASK) == MEL_MSGBOX_ERROR; }
static inline bool mel_msgbox_warned(Mel_Msgbox_Status s) { return (s & MEL_MSGBOX_SEVERITY_MASK) == MEL_MSGBOX_WARNED; }

typedef u32 Mel_Msgbox_Severity;

#define MEL_MSGBOX_SEVERITY_INFO  0u
#define MEL_MSGBOX_SEVERITY_WARN  1u
#define MEL_MSGBOX_SEVERITY_ERROR 2u

typedef struct
{
    str8 label;
    i32  id;
} Mel_Msgbox_Button;

typedef struct
{
    bool       has_value;
    mel_color8 value;
} Mel_Msgbox_Color;

typedef struct
{
    str8                title;
    str8                message;
    Mel_Msgbox_Severity severity;

    const Mel_Msgbox_Button* buttons;
    u32                      button_count;

    i32  default_id;
    bool has_default_id;
    i32  escape_id;
    bool has_escape_id;

    Mel_Msgbox_Color accent;
    Mel_Msgbox_Color text;
    Mel_Msgbox_Color background;

    bool       right_to_left;
    Mel_Window parent;
} Mel_Msgbox_Opt;

typedef struct
{
    i32               chosen_id;
    Mel_Msgbox_Status status;
} Mel_Msgbox_Result;

bool mel_msgbox_available(void);

Mel_Msgbox_Status mel_msgbox_alert_opt(str8 title, str8 message, Mel_Msgbox_Opt opt);
#define mel_msgbox_alert(title, message, ...) mel_msgbox_alert_opt((title), (message), (Mel_Msgbox_Opt){ __VA_ARGS__ })

Mel_Msgbox_Result mel_msgbox_show_opt(Mel_Msgbox_Opt opt);
#define mel_msgbox_show(...) mel_msgbox_show_opt((Mel_Msgbox_Opt){ __VA_ARGS__ })

#ifdef __cplusplus
}
#endif
