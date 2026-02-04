#ifndef MEL_UI_PANEL_H
#define MEL_UI_PANEL_H

#include "ui_widget.h"

typedef struct
{
    Mel_Widget base;
} Mel_Panel;

void mel_panel_init(Mel_Panel* panel);

#endif
