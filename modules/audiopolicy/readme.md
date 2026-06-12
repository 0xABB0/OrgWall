# audiopolicy

OS audio routing and arbitration policy: session category and mode, output
override, bluetooth options, mixing/ducking, audio focus, interruptions,
route-change events. One module because the OS session object spans input
and output at once; `audiomixer`, `audiocapture`, `tts`/`stt` honor the applied
policy, none of them owns it.

One internal backend per platform behind a vtable: AVAudioSession (ios, the
fullest), audio focus + communication mode via JNI (android), an autoplay
probe context surfacing interruptions (wasm), honest lowering with named
warnings plus default-device route events (macos), communications-role
ducking acknowledged under voice_chat (win32), fully honest-absent (linux).
Every knob a platform lacks is named in warning bits, never dropped.
See `spec.md` for the full contract.

Dependencies: `core`, `allocator`, `collection`, `executor`, `event`,
`log`; `platform` on android.
