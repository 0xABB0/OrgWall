#include "audiopolicy_internal.h"

const mel_audiopolicy_category mel_audiopolicy_playback = { .name = "playback" };
const mel_audiopolicy_category mel_audiopolicy_record = { .name = "record" };
const mel_audiopolicy_category mel_audiopolicy_duplex = { .name = "duplex" };
const mel_audiopolicy_category mel_audiopolicy_ambient = { .name = "ambient" };

const mel_audiopolicy_mode mel_audiopolicy_mode_default = { .name = "default" };
const mel_audiopolicy_mode mel_audiopolicy_mode_voice_chat = { .name = "voice_chat" };
const mel_audiopolicy_mode mel_audiopolicy_mode_video_chat = { .name = "video_chat" };
const mel_audiopolicy_mode mel_audiopolicy_mode_measurement = { .name = "measurement" };
const mel_audiopolicy_mode mel_audiopolicy_mode_media = { .name = "media" };

const mel_audiopolicy_output mel_audiopolicy_output_default = { .name = "default" };
const mel_audiopolicy_output mel_audiopolicy_output_speaker = { .name = "speaker" };

const mel_audiopolicy_route_reason mel_audiopolicy_route_device_added = { .name = "device_added" };
const mel_audiopolicy_route_reason mel_audiopolicy_route_device_removed = { .name = "device_removed" };
const mel_audiopolicy_route_reason mel_audiopolicy_route_category_changed = { .name = "category_changed" };
const mel_audiopolicy_route_reason mel_audiopolicy_route_override = { .name = "override" };
const mel_audiopolicy_route_reason mel_audiopolicy_route_unknown = { .name = "unknown" };

const char* mel_audiopolicy_category_name(const mel_audiopolicy_category* c) { return c ? c->name : "none"; }

const char* mel_audiopolicy_mode_name(const mel_audiopolicy_mode* m) { return m ? m->name : "default"; }

const char* mel_audiopolicy_route_reason_name(const mel_audiopolicy_route_reason* r) { return r ? r->name : "unknown"; }
