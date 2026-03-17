#pragma once

#include "ui.widget.h"
#include "math.vec4.h"
#include "font.atlas.fwd.h"
#include "string.str8.fwd.h"

typedef struct Mel_WEdit Mel_WEdit;

typedef void (*Mel_WEdit_Change_Fn)(Mel_WEdit* edit, void* user_data);
typedef void (*Mel_WEdit_Confirm_Fn)(Mel_WEdit* edit, void* user_data);

struct Mel_WEdit {
    Mel_Widget base;
    Mel_Font_Atlas_Handle font;
    Mel_Vec4 bg_color;
    Mel_Vec4 border_color;
    Mel_Vec4 focus_border_color;
    Mel_Vec4 text_color;
    Mel_Vec4 placeholder_color;
    Mel_WEdit_Change_Fn on_change;
    Mel_WEdit_Confirm_Fn on_confirm;
    void* user_data;
    char text[256];
    u32 text_len;
    char placeholder[128];
    u32 placeholder_len;
};

void mel_wedit_init(Mel_WEdit* edit);
void mel_wedit_set_text(Mel_WEdit* edit, str8 text);
void mel_wedit_set_placeholder(Mel_WEdit* edit, str8 text);
str8 mel_wedit_get_text(Mel_WEdit* edit);
