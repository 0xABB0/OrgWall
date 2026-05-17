#pragma once

#include "ui.widget.h"
#include "math.vec4.h"
#include "font.atlas.fwd.h"
#include "str8.fwd.h"

typedef struct {
    Mel_Widget base;
    Mel_Font_Atlas_Handle font;
    str8 text;
    Mel_Vec4 text_color;
} Mel_WLabel;

void mel_wlabel_init(Mel_WLabel* label);
void mel_wlabel_set_text(Mel_WLabel* label, str8 text);
