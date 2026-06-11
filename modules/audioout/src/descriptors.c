#include "audioout_internal.h"

const mel_audioout_kind mel_audioout_builtin = { .name = "builtin" };
const mel_audioout_kind mel_audioout_hdmi = { .name = "hdmi" };
const mel_audioout_kind mel_audioout_usb = { .name = "usb" };
const mel_audioout_kind mel_audioout_bluetooth = { .name = "bluetooth" };
const mel_audioout_kind mel_audioout_virtual = { .name = "virtual" };
const mel_audioout_kind mel_audioout_unknown = { .name = "unknown" };

const char* mel_audioout_kind_name(const mel_audioout_kind* k) { return k ? k->name : "unknown"; }
