#include <hardware.fleshlight/fleshlight_priv.h>

#include <core/platform.h>
#include <core/types.h>

#if MEL_PLATFORM_WINDOWS

// ── Windows BLE backend (stub) ─────────────────────────────────────────────
//
// To implement:
// 1. Use Windows.Devices.Bluetooth.Advertisement API (C++/WinRT or COM)
// 2. Scan for BLE advertisements matching Fleshlight service UUIDs
// 3. Connect via BluetoothLEDevice::FromBluetoothAddressAsync
// 4. Get GATT service/characteristic via GetGattServicesForUuidAsync
// 5. Write with GattCharacteristic::WriteValueAsync
//    See src/macos/fleshlight_macos.c for protocol packet format.
//
// Alternative: use the buttplug-rs FFI (buttplug.io has a Rust core with C FFI).

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

typedef int __mel_fleshlight_win32_only;

#endif
