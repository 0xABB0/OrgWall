#ifndef MEL_UI_LABEL_H
#define MEL_UI_LABEL_H

#include "ui_widget.h"

typedef struct Mel_Font Mel_Font;

typedef struct
{
    Mel_Widget base;
    Mel_Font* font;
    const char* text;
    Mel_Vec4 text_color;
} Mel_Label;

void mel_label_init(Mel_Label* label);
void mel_label_set_text(Mel_Label* label, const char* text);

#endif
