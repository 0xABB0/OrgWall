#pragma once

#include <gamepad/joystick.h>
#include <gamepad/provider.h>

bool mel_joystick__lookup(Mel_Joystick j, u32* out_provider_idx, u64* out_stable_id);

const Mel_Joystick_Descriptor* mel_joystick__descriptor(Mel_Joystick j);

const Mel_Joystick_Provider_Desc* mel_joystick__provider_desc(u32 provider_idx);

typedef void (*Mel_Joystick_Host_Register_Fn)(const Mel_Alloc* alloc);

void mel_joystick__set_host_register(Mel_Joystick_Host_Register_Fn fn);
