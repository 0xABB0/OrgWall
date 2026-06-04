#pragma once

#include <messagebox/messagebox.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    str8                     title;
    str8                     message;
    Mel_Msgbox_Severity      severity;
    const Mel_Msgbox_Button* buttons;
    u32                      button_count;
    i32                      default_id;
    i32                      escape_id;
    Mel_Msgbox_Color         accent;
    Mel_Msgbox_Color         text;
    Mel_Msgbox_Color         background;
    bool                     right_to_left;
    void*                    native_parent;
} Mel_Msgbox_Request;

bool mel_msgbox__plat_available(void);

Mel_Msgbox_Status mel_msgbox__plat_show(const Mel_Msgbox_Request* req, i32* out_chosen_id);

#ifdef __cplusplus
}
#endif
