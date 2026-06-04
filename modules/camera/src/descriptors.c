#include "descriptors_internal.h"

const mel_camera_facing mel_camera_front    = { .name = "front" };
const mel_camera_facing mel_camera_back     = { .name = "back" };
const mel_camera_facing mel_camera_external = { .name = "external" };
const mel_camera_facing mel_camera_unknown  = { .name = "unknown" };

const mel_camera_auth mel_camera_auth_granted        = { .name = "granted", .granted = true };
const mel_camera_auth mel_camera_auth_denied         = { .name = "denied", .granted = false };
const mel_camera_auth mel_camera_auth_not_determined = { .name = "not_determined", .granted = false };
const mel_camera_auth mel_camera_auth_restricted     = { .name = "restricted", .granted = false };

const char* mel_camera_facing_name(const mel_camera_facing* f) { return f ? f->name : "unknown"; }

const char* mel_camera_auth_name(const mel_camera_auth* a) { return a ? a->name : "unknown"; }

bool mel_camera_auth_is_granted(const mel_camera_auth* a) { return a && a->granted; }
