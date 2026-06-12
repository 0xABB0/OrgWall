# tts

Text-to-speech, and nothing else: enumerate voices across providers, speak
with rate/pitch/volume multipliers, SSML input, word-boundary and viseme
marks, pause/resume/abort, offline render to caller-owned PCM. Split from
the former `speech` module — synthesis shares nothing operational with
recognition (no consent, no microphone, different lowering), so a
screen-reader app links synthesis alone.

Provider-plugin shaped like `audioin`: host OS engines are provider 0
(registered by a platform-selected TU via
`mel_tts__register_host_providers`); cloud and local engines register
identically through `<tts/provider.h>`. The core lowers every request onto
the voice's declared caps and names each loss with a warning bit; SSML and
render are functional gates, never silent fidelity drops. Terminal
callbacks fire exactly once, guarded against late provider completions.
Rendered PCM is borrowed, valid only during the callback. See `spec.md`.

Dependencies: `core`, `allocator`, `collection`, `string`, `log`,
`thread`; `platform` on android.
