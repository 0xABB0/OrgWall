#pragma once

#include <layout/layout.h>

#include <gui/handle.h>

/* Attach a layout to a container; the container takes ownership. On a backend
 * whose platform owns layout idiomatically (CSS flex on web, LinearLayout on
 * Android) a recognised layout class is lowered to the native engine; anywhere
 * else — and for any class the backend does not recognise — the portable
 * solver arranges and pushes absolute bounds. Same description, both honest. */
void mel_gui_set_layout(Mel_Gui_Handle parent, Mel_Layout* layout);
void mel_gui_relayout(Mel_Gui_Handle handle);
