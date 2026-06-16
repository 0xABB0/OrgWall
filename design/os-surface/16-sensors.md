# Sensors & physical-world I/O — OS-surface atlas (finer grain)
> domains D35–D40 (D38 camera = designed stub). Consumed by 00-atlas.md. Capability ceiling, axis-neutral; platform APIs are hints.

### D35 · sensor-motion — inertial & motion
def: device-motion sensing and fusion.
- **raw inertial**: · accelerometer · gyroscope · magnetometer/compass · uncalibrated variants (raw + bias) (CoreMotion / Android SensorManager TYPE_*_UNCALIBRATED)
- **fusion**: · attitude/rotation-vector quaternion · linear-accel/gravity split · game-rotation (no-mag) · geomagnetic-rotation · device-orientation (yaw/pitch/roll)
- **derived activity**: · step counter · step detector · pedometer (distance/floors/cadence) · activity/motion classification (still/walk/run/cycle/vehicle) · significant-motion trigger
- **sampling & delivery**: · sampling-rate / output-data-rate request · hardware FIFO batching & flush · per-sample timestamps & sensor clock domain · on-change vs continuous reporting · max-range & resolution query
- **calibration**: · calibration state & accuracy level · bias/offset reporting · soft/hard-iron magnetometer cal · recalibration prompts
↑beyond: raw uncalibrated streams · hardware FIFO batching · sensor-hub offload.
apps: AR · fitness · games (tilt) · navigation · camera OIS correlation.
status: spawn (`sensor` domain).

### D36 · sensor-env — environmental sensors
def: ambient physical-quantity sensors.
- **light**: · ambient illuminance (lux) · correlated color-temperature · per-channel RGB/clear · flicker
- **proximity**: · near/far boolean · distance (cm) where exposed
- **pressure**: · barometric pressure · relative-altitude derivation · sea-level reference
- **thermal/humidity**: · ambient temperature · relative humidity · dew-point derivation
- **discrete/misc**: · hall / lid (open-close) · UV index · heart-rate (where wrist-exposed)?
- **delivery**: · sampling rate · event thresholds / hysteresis · on-change reporting · accuracy level
apps: auto-brightness · weather · fitness · well-being apps.
status: spawn (`temperature`/`frequency` partial domains).

### D37 · location — positioning & geofencing
def: where the device is.
- **fix sources**: · GNSS satellite · network/cell/wifi · fused provider (CoreLocation / FusedLocation) · passive (piggyback) · last-known cache
- **fix payload**: · lat/lon · horizontal/vertical accuracy · altitude (ellipsoid/MSL) · heading/course (true vs magnetic) · speed + accuracy · timestamp
- **authorization tiers**: · precise vs coarse/approximate · when-in-use vs always · temporary/one-time grant · background-location entitlement · reduced-accuracy toggle
- **region monitoring**: · circular geofence enter/exit · region dwell · max-region limits query · significant-change monitoring · visit detection
- **ranging/proximity**: · beacon ranging (iBeacon / Eddystone) · beacon region monitoring · indoor positioning?
- **integrity**: · mock/spoofed-location detection · location-services-enabled state · coordinate datum/CRS handling (WGS84)
↑beyond: raw GNSS measurements / carrier phase (RTK) · dual-frequency.
apps: maps/navigation · fitness tracking · ride-share · geofenced automation.
status: spawn (`geolocation` domain; `apps/geo-tour`).

### D38 · camera — image/video capture & egress
def: live & still capture from camera devices, plus virtual-camera publish.
- DESIGNED — full treatment in `design/camera/` (charter→freeze v1). 14 areas: devices & enumeration · topology & streams · fine-grained control · mechanical PTZ · capture modes · depth/3D/calibration · live effects · frame memory · timing · metadata · egress · test ingest · OS integration.
↑beyond/↓under: uvc-direct · genicam · embedded+baresensor (see charter).
status: **designed** → `design/camera/`

### D39 · proximity-nfc — NFC & short-range tags
def: near-field reading, writing, and emulation.
- **tag read/write**: · NDEF record read · NDEF write/format · NDEF make-read-only · low-level/raw transceive (CoreNFC / Android NFC)
- **tag technologies**: · ISO-DEP (ISO14443-4) · NfcA/B/F/V · MIFARE Classic/Ultralight · FeliCa · ISO15693 vicinity
- **emulation**: · host-card emulation (HCE) · AID routing & registration · payment/default-wallet (gated) · secure-element/SIM-based emulation (gated)
- **modes**: · reader/writer mode · peer/P2P (legacy SNEP/LLCP)? · card-emulation mode
- **session & UX**: · scan-session start/stop · OS scan UI prompt & message · polling tech selection · timeout/error surfacing
- **dispatch**: · foreground tag dispatch · background tag/NDEF launch · app-record/AAR routing
↑beyond: raw APDU · secure-element access.
apps: transit · access control · inventory · payment · pairing.
status: none.

### D40 · scan-code — barcode / document detection
def: turning camera frames into codes/structured detections (OS-provided detectors).
- **barcode**: · 1D linear (EAN/UPC/Code128/Code39/ITF) · 2D (QR/Aztec/DataMatrix/PDF417) · payload decode & type · multi-code per frame (Vision-barcode / MLKit / Android ML Kit)
- **symbology config**: · enabled-symbology selection · checksum/format options · inverted/mirrored handling
- **document/shape**: · rectangle/quad detection · document/page detection · perspective correction & dewarp · edge/contour
- **text regions**: · text/OCR region detection (boundary with D74 vision) · region grouping (line/block)
- **input & timing**: · live-stream vs still-image detection · per-frame vs batched · ROI/scan-region constraint
- **result**: · bounding geometry (corners/quad) · confidence score · orientation/angle · tracking-id across frames?
apps: retail scanners · document scanners · ticketing.
status: spawn (`barcode` domain; `apps/barcode-*`).
