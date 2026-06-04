#include <input/events.h>

#include <string/str8.h>

#include "input_internal.h"

u32 mel_input_events__changed_fields(const Mel_Input_Device_Descriptor* a, const Mel_Input_Device_Descriptor* b)
{
    u32 f = 0;
    if (!str8_equals(a->name, b->name))
        f |= MEL_INPUT_FIELD_NAME;
    if (a->caps != b->caps)
        f |= MEL_INPUT_FIELD_CAPS;
    if (a->key_count != b->key_count)
        f |= MEL_INPUT_FIELD_LAYOUT;
    if (a->button_count != b->button_count)
        f |= MEL_INPUT_FIELD_BUTTONS;
    if (a->touch_point_max != b->touch_point_max || a->touch_direct != b->touch_direct || a->touch_indirect != b->touch_indirect)
        f |= MEL_INPUT_FIELD_TOUCH;
    if (a->pen_button_count != b->pen_button_count || a->pressure_max != b->pressure_max || a->hover_distance_max != b->hover_distance_max)
        f |= MEL_INPUT_FIELD_PEN;
    return f;
}
