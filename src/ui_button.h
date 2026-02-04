#ifndef MEL_UI_BUTTON_H
#define MEL_UI_BUTTON_H

#include "ui_widget.h"

typedef void (*Mel_ButtonCallback)(void* user);

typedef struct
{
    Mel_Widget base;
    Mel_Vec4 normal_color;
    Mel_Vec4 hover_color;
    Mel_Vec4 pressed_color;
    bool hovered;
    bool pressed;
    Mel_ButtonCallback on_click;
    void* user_data;
} Mel_Button;

void mel_button_init(Mel_Button* button);

#endif
