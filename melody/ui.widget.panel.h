#pragma once

#include "ui.widget.h"
#include "math.vec4.h"

typedef struct {
    Mel_Widget base;
    Mel_Vec4 color;
} Mel_WPanel;

void mel_wpanel_init(Mel_WPanel* panel);
