#pragma once

#include "ui.widget.h"
#include "math.vec4.h"

typedef void (*Mel_WButton_Click_Fn)(void* user_data);

typedef struct {
    Mel_Widget base;
    Mel_Vec4 normal_color;
    Mel_Vec4 hover_color;
    Mel_Vec4 pressed_color;
    Mel_WButton_Click_Fn on_click;
    void* click_data;
} Mel_WButton;

void mel_wbutton_init(Mel_WButton* button);
