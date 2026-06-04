# sensor

Standalone IMU access: 3-axis accelerometer (m/s^2, gravity included) and 3-axis gyroscope (rad/s),
including per-Joy-Con L/R variants reported as an open `side` field on the device caps. Push (event)
and pull (poll) faces over a self-contained device spine, honest absence where no IMU exists
(MEL-ENGINE-VIII).

## Why it exists

Games and apps want motion: tilt, shake, orientation, controller gyro aim. Every platform exposes an
IMU through a different surface (Core Motion, Industrial-IO, SensorManager, the Windows Sensor API,
the Web Sensor API). This module unifies them behind one handle-based registry with hot-plug events
and a single sample shape, in SI units, without lying about capabilities a device lacks.

## Public surface

- `<sensor/sensor.h>` — `Mel_Sensor` generational handle, `Mel_Sensor_Caps`/`Mel_Sensor_Descriptor`,
  `Mel_Sensor_Reading`, `Mel_Sensor_Status` bitset, init/shutdown, refresh/count/list, describe,
  rates query, `mel_sensor_start`/`stop`/`streaming`/`read`, `mel_sensor_native`.
- `<sensor/events.h>` — hot-plug + sample events: `mel_sensor_poll_events` (pull) and
  `mel_sensor_subscribe`/`unsubscribe` (push, onto a `Mel_Executor`).
- `<sensor/provider.h>` — provider registration (`Mel_Sensor_Provider_Desc`) so foreign sources
  (controller IMUs over the input spine, virtual devices) feed the same registry.

Units: accelerometer m/s^2 with gravity included; gyroscope rad/s. `valid_mask` says which streams a
given sample carries. `side` uses the open `MEL_SENSOR_SIDE_*` value space (left/right for Joy-Cons).

## Spine note (merge-time unification with `input`)

Per the pilot buildability rule, this module self-contains a minimal device spine (generational
handle, registry-owned slotmap, hot-plug diff, provider registration, push/pull event channel)
consistent with the ratified device-spine contract. When the canonical `input` module lands, this
spine collapses onto it: `Mel_Sensor` keeps its handle and caps, but the registry, event channel,
and provider model unify with the shared input spine. Only `input` defines the canonical spine.

## Backends

- macos — no built-in IMU API; honest-absent host provider (enumerates zero).
- ios — Core Motion `CMMotionManager` (raw accelerometer in G -> m/s^2, gyro rad/s).
- linux — Industrial-IO sysfs (`/sys/bus/iio/devices`, `in_accel_*` / `in_anglvel_*` raw x scale).
- android — JNI `SensorManager` (`TYPE_ACCELEROMETER` / `TYPE_GYROSCOPE`), listener pushes samples.
- win32 — COM Sensor API (`ISensorManager`, accelerometer 3D / gyrometer 3D); honest-absent when the
  toolchain lacks `<sensorsapi.h>`.
- wasm — Generic Sensor API (`Accelerometer`/`Gyroscope`), DeviceMotion fallback (deg/s -> rad/s).

## Dependencies

core, allocator, collection, string, event, executor, log, platform.
