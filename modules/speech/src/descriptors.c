#include "descriptors_internal.h"

const mel_speech_auth mel_speech_auth_granted = { .name = "granted", .granted = true };
const mel_speech_auth mel_speech_auth_denied = { .name = "denied", .granted = false };
const mel_speech_auth mel_speech_auth_not_determined = { .name = "not_determined", .granted = false };
const mel_speech_auth mel_speech_auth_restricted = { .name = "restricted", .granted = false };

const char* mel_speech_auth_name(const mel_speech_auth* a) { return a ? a->name : "unknown"; }

bool mel_speech_auth_is_granted(const mel_speech_auth* a) { return a && a->granted; }
