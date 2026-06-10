# audiocapture — owed

- Platforms: iOS (AVAudioSession + AudioQueue), linux (ALSA), win32 (WASAPI capture),
  android (AAudio input), wasm (getUserMedia + AudioWorklet).
- Async permission request returning a `Mel_Future` (mirror camera's
  `mel_camera_authorize`); today the OS prompt fires implicitly on first open.
- Device hotplug subscription (mirror `mel_camera_subscribe`).
- Multi-channel capture; only mono is exposed.
- Extract the SPSC f32 ring shared with `audio` into a common home instead of two
  private copies.
