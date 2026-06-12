#include "audioin_internal.h"

const mel_audioin_kind mel_audioin_builtin = { .name = "builtin" };
const mel_audioin_kind mel_audioin_usb = { .name = "usb" };
const mel_audioin_kind mel_audioin_bluetooth = { .name = "bluetooth" };
const mel_audioin_kind mel_audioin_virtual = { .name = "virtual" };
const mel_audioin_kind mel_audioin_loopback = { .name = "loopback" };
const mel_audioin_kind mel_audioin_unknown = { .name = "unknown" };

const mel_audioin_auth mel_audioin_auth_granted = { .name = "granted", .granted = true, .restrictiveness = 0 };
const mel_audioin_auth mel_audioin_auth_not_determined = { .name = "not_determined", .granted = false, .restrictiveness = 1 };
const mel_audioin_auth mel_audioin_auth_restricted = { .name = "restricted", .granted = false, .restrictiveness = 2 };
const mel_audioin_auth mel_audioin_auth_denied = { .name = "denied", .granted = false, .restrictiveness = 3 };

const char* mel_audioin_kind_name(const mel_audioin_kind* k) { return k ? k->name : "unknown"; }

const char* mel_audioin_auth_name(const mel_audioin_auth* a) { return a ? a->name : "unknown"; }

bool mel_audioin_auth_is_granted(const mel_audioin_auth* a) { return a && a->granted; }
