#include "events_internal.h"

#include <string.h>

u32 mel_hid_events__changed_fields(const Mel_Hid_Descriptor* a, const Mel_Hid_Descriptor* b)
{
    u32 f = 0;
    if (a->vendor_id != b->vendor_id || a->product_id != b->product_id || a->version_bcd != b->version_bcd)
        f |= MEL_HID_FIELD_IDENTITY;
    if (a->usage_page != b->usage_page || a->usage != b->usage)
        f |= MEL_HID_FIELD_USAGE;
    if (a->input_report_len != b->input_report_len || a->output_report_len != b->output_report_len || a->feature_report_len != b->feature_report_len || a->has_report_id != b->has_report_id || a->report_id_count != b->report_id_count)
        f |= MEL_HID_FIELD_REPORTS;
    if (strncmp(a->manufacturer, b->manufacturer, MEL_HID_STRING_CAP) != 0 || strncmp(a->product, b->product, MEL_HID_STRING_CAP) != 0 || strncmp(a->serial, b->serial, MEL_HID_STRING_CAP) != 0)
        f |= MEL_HID_FIELD_STRINGS;
    if (a->bus != b->bus)
        f |= MEL_HID_FIELD_BUS;
    return f;
}
