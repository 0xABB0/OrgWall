#pragma once

#include "ui.widget.h"
#include "math.vec4.h"

typedef struct Mel_Font Mel_Font;

typedef struct {
    Mel_Widget base;
    Mel_Font* font;
    const char* text;
    Mel_Vec4 text_color;
} Mel_WLabel;

void mel_wlabel_init(Mel_WLabel* label);
void mel_wlabel_set_text(Mel_WLabel* label, const char* text);
