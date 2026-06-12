#include "stt_internal.h"

const mel_stt_auth mel_stt_auth_granted = { .name = "granted", .granted = true, .restrictiveness = 0 };
const mel_stt_auth mel_stt_auth_not_determined = { .name = "not_determined", .granted = false, .restrictiveness = 1 };
const mel_stt_auth mel_stt_auth_restricted = { .name = "restricted", .granted = false, .restrictiveness = 2 };
const mel_stt_auth mel_stt_auth_denied = { .name = "denied", .granted = false, .restrictiveness = 3 };

const char* mel_stt_auth_name(const mel_stt_auth* a) { return a ? a->name : "unknown"; }

bool mel_stt_auth_is_granted(const mel_stt_auth* a) { return a && a->granted; }
