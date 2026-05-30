#pragma once

#include <display/display.h>
#include <display/events.h>

void mel_display_events__reset(void);
void mel_display_events__emit(Mel_Display_Event ev);
u32  mel_display_events__changed_fields(const Mel_Display_Descriptor* a,
                                        const Mel_Display_Descriptor* b);
