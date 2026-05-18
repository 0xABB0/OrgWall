# hardware.fleshlight

BLE-controlled teledildonics integration. Cross-platform: macOS (CoreBluetooth), Linux (BlueZ/D-Bus), Windows (WinRT BLE), Android (JNI BLE).

## API

```c
#include <hardware.fleshlight/fleshlight.h>

i32 count = mel_fleshlight_enumerate(infos, 128);
Mel_Fleshlight_Device* dev = mel_fleshlight_open(infos[0].id);

mel_fleshlight_set_position(dev, 0.75f);  // linear actuator 75%
mel_fleshlight_set_speed(dev, 0.5f);      // speed 50%
mel_fleshlight_stop(dev);                 // emergency stop

mel_fleshlight_close(dev);
```

## Supported devices

| Kind | Capabilities | Transport |
|------|-------------|-----------|
| `MEL_FLESHLIGHT_KIND_LAUNCH` | position + speed | BLE |
| `MEL_FLESHLIGHT_KIND_MAX2` | speed | BLE |
| `MEL_FLESHLIGHT_KIND_KEON` | position + speed | BLE |

## Protocol

Kiiroo/Fleshlight devices use a common BLE protocol:

- **Service UUID:** `88F80580-0000-01E6-AACE-0002A5D5C51B`
- **TX (write):** `88F80581-...`
- **RX (notify):** `88F80582-...`

20-byte command packets:
```
[0]    device type (0x01=Launch, 0x03=Keon)
[1]    command (0x01=position, 0x02=speed)
[2:3]  value (0-99, little-endian u16)
[4:19] padding (zeros)
```

Ref: https://github.com/metafetish/buttplug-device-config

## Platform status

- **macOS:** stub — needs CoreBluetooth implementation
- **Linux:** stub — needs BlueZ D-Bus implementation
- **Windows:** stub — needs WinRT BLE implementation
- **Android:** stub — needs JNI BLE implementation

Each platform backend is in `src/<platform>/fleshlight_<platform>.c` with implementation notes.
