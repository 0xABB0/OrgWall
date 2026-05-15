#pragma once

#include <gui/gui.h>

typedef enum Mel_Gui_Accessibility_Role {
    MEL_GUI_A11Y_ROLE_NONE,
    MEL_GUI_A11Y_ROLE_WINDOW,
    MEL_GUI_A11Y_ROLE_PANEL,
    MEL_GUI_A11Y_ROLE_LABEL,
    MEL_GUI_A11Y_ROLE_BUTTON,
    MEL_GUI_A11Y_ROLE_TEXT_FIELD,
    MEL_GUI_A11Y_ROLE_CHECKBOX,
    MEL_GUI_A11Y_ROLE_SLIDER,
} Mel_Gui_Accessibility_Role;

typedef enum Mel_Gui_Accessibility_State {
    MEL_GUI_A11Y_STATE_NONE     = 0,
    MEL_GUI_A11Y_STATE_DISABLED = 1u << 0,
    MEL_GUI_A11Y_STATE_FOCUSED  = 1u << 1,
    MEL_GUI_A11Y_STATE_CHECKED  = 1u << 2,
    MEL_GUI_A11Y_STATE_SELECTED = 1u << 3,
} Mel_Gui_Accessibility_State;

typedef struct Mel_Gui_Accessibility_Desc {
    Mel_Gui_Accessibility_Role role;
    str8 name;
    str8 value;
    str8 hint;
    u32 state;
} Mel_Gui_Accessibility_Desc;
