# Audio & music — OS-surface atlas (finer grain)
> domains D23–D26. Consumed by 00-atlas.md. Capability ceiling, axis-neutral; platform APIs are hints.

---

### D23 · audio-out — playback & output routing
def: get PCM/encoded audio to the speakers with controlled latency.
- **device** — enumerate, default-device tracking, hotplug
- **formats** — sample rate / channel / bit-depth negotiation
- **latency**:
  · shared-mixed (system mixer)
  · low-latency callback (AAudio / CoreAudio render cb / OpenSL)
  · exclusive bit-perfect (WASAPI-exclusive / CoreAudio HAL / ASIO?)
- **spatial**:
  · channel-based 5.1 / 7.1
  · object-based Atmos
  · binaural HRTF
- **routing**: speaker · headphone · Bluetooth · HDMI · AirPlay · default-change
- **processing**: per-stream volume · EQ/effects tap · loudness
↑beyond: ASIO · JACK · exclusive bit-perfect · hardware DSP offload
apps: DAWs · players · games (3D audio) · conferencing
status: spawn (audioout / audiomixer / audioplayback domains)

---

### D24 · audio-in — capture & input DSP
def: pull PCM from microphones and input devices.
- **device** — enumerate, default-input tracking, hotplug
- **formats** — sample rate / channel / bit-depth negotiation
- **latency**:
  · shared (system input path)
  · low-latency callback (AAudio input / CoreAudio input cb / OpenSL recorder)
  · exclusive / pro (WASAPI-exclusive · ASIO?)
- **voice-dsp**:
  · acoustic echo cancellation (AEC)
  · noise suppression (NS)
  · automatic gain control (AGC)
  · voice isolation / focus
  · de-reverb?
- **gain** — input level / boost · mute
- **multi-mic**:
  · multi-channel capture
  · beamforming / directional
  · raw mic-array access?
- **loopback** — system-audio / output capture (loopback device · ScreenCaptureKit audio · WASAPI-loopback · submix capture)
- **virtual-input** — publish-as-virtual-mic (egress, mirrors camera publish)
- **consent** — mic authorization · in-use indicator
↑beyond: raw mic-array access · ambisonics capture
apps: conferencing · voice recorders · DAWs · OBS
status: spawn (audioin / audiocapture / pcm domains)

---

### D25 · audio-policy — session, focus & interruptions
def: arbitrate audio between apps and routes.
- **session** — category / mode / usage-attribute declaration (AVAudioSession category · AudioAttributes · stream type)
- **focus**:
  · request / abandon focus
  · gain / loss / transient-loss events
  · loss-with-duck grant
- **policy** — mixing-with-others · duck-others · interrupt-spoken-audio
- **interruptions**:
  · phone-call / VoIP interruption
  · alarm / timer interruption
  · begin / end & should-resume hint
- **route-change** — route-change events · old-device-unavailable (headphone unplug) · override reason
- **now-playing**:
  · metadata publish (title / artist / artwork)
  · transport commands (play / pause / next / prev / seek / scrub)
  · lock-screen / control-center / media-notification surface (MediaSession · SMTC · MPNowPlayingInfoCenter)
- **respect** — silent-switch / ringer respect · system-volume observe
- **background-audio** — background-playback entitlement / declaration
apps: media players · games · conferencing · podcast apps
status: spawn (audiopolicy domain)

---

### D26 · midi — MIDI I/O & timing
def: musical control messages and devices.
- **discovery** — device & port enumeration · hotplug / connect-disconnect
- **midi1** — channel-voice / control-change / program-change messages · running status
- **midi2**:
  · UMP (universal MIDI packet) transport
  · per-note controllers / high-res
  · property exchange
  · MIDI-CI capability negotiation
- **mpe** — per-note pitch-bend / pressure expression
- **timing**:
  · timestamping (send / receive)
  · MIDI clock / transport (start / stop / continue)
  · jitter / latency reporting
- **sysex** — system-exclusive send / receive · bulk transfer
- **virtual** — publish virtual endpoints (source / destination)
- **transports**:
  · USB-MIDI
  · Bluetooth-MIDI (BLE-MIDI)
  · network (RTP-MIDI / AppleMIDI)
  · Web MIDI (browser axis)
apps: DAWs · sequencers · lighting/show control · instrument apps
status: spawn (midi domain; apps/midi-monitor)
