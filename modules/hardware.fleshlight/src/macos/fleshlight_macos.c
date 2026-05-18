#include <hardware.fleshlight/fleshlight_priv.h>

#include <core/platform.h>
#include <core/types.h>

#if MEL_PLATFORM_OSX

// ── macOS CoreBluetooth backend ────────────────────────────────────────────
//
// This is a stub. To implement:
//
// 1. Create a CBCentralManager on the main dispatch queue
// 2. Scan for BLE peripherals advertising the Fleshlight Launch service:
//    - Service UUID: 88F80580-0000-01E6-AACE-0002A5D5C51B
//    - TX characteristic: 88F80581-...  (write)
//    - RX characteristic: 88F80582-...  (notify)
// 3. On discovery, populate Mel_Fleshlight_Info with:
//    - kind = MEL_FLESHLIGHT_KIND_LAUNCH
//    - supports_position = true, supports_speed = true
// 4. On connect, discover services/characteristics and cache CBPeripheral + CBCharacteristic
// 5. The Launch protocol sends 20-byte packets:
//    - Byte 0:   device ID (0x01 = Launch, 0x03 = Keon)
//    - Byte 1:   command (0x01 = set position, 0x02 = set speed)
//    - Byte 2-3: position (0-99) or speed (0-99) as little-endian u16
//    - Bytes 4-19: padding zeros
// 6. Full protocol docs: https://github.com/metafetish/buttplug-device-config
//
// Note: CoreBluetooth requires an NSApplication run loop. This module
// assumes the caller already has one (e.g., from the gui.platform.macos module).

#include <stdlib.h>

i32 mel_fleshlight_platform_enumerate(Mel_Fleshlight_Info* out_infos, i32 max_count)
{
    MEL_UNUSED(out_infos);
    MEL_UNUSED(max_count);
    return 0;
}

Mel_Fleshlight_Device* mel_fleshlight_platform_open(i32 id, Mel_Fleshlight_Device* device)
{
    MEL_UNUSED(id);
    MEL_UNUSED(device);
    return NULL;
}

void mel_fleshlight_platform_close(Mel_Fleshlight_Device* device)
{
    MEL_UNUSED(device);
}

bool mel_fleshlight_platform_set_position(Mel_Fleshlight_Device* device, f32 position)
{
    MEL_UNUSED(device);
    MEL_UNUSED(position);
    return false;
}

bool mel_fleshlight_platform_set_speed(Mel_Fleshlight_Device* device, f32 speed)
{
    MEL_UNUSED(device);
    MEL_UNUSED(speed);
    return false;
}

bool mel_fleshlight_platform_stop(Mel_Fleshlight_Device* device)
{
    MEL_UNUSED(device);
    return false;
}

#else

typedef int __mel_fleshlight_macos_only; // suppress empty TU warning

#endif
