#pragma once

#include <hid/hid.h>
#include <hid/provider.h>

// Resolve the live native channel for an opened handle, or NULL when the handle is dead or closed.
// Backends never call this; it is the seam the async read path uses to fetch the pollable fd.
bool mel_hid__channel(Mel_Hid_Device d, Mel_Hid_Channel* out_channel, u32* out_provider_idx, u64* out_stable_id);

// Drive one synchronous read through the provider that owns the handle. The async fallback source
// calls this once the readiness source fires (or immediately when no fd backend exists).
Mel_Hid_Io_Result mel_hid__read_now(Mel_Hid_Device d, u8* out, usize cap, i32 timeout_ms);

// Test seam: suppress native host-provider registration so a unit test can drive the registry/diff
// and the full I/O surface through a fake provider, hardware-free and fork-safe.
void mel_hid__set_skip_host_providers(bool skip);
