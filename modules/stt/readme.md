# stt

Speech-to-text, and nothing else: enumerate per-language recognizers across
providers, authorize, stream partial and final transcripts. Split from the
former `speech` module — recognition's audio story is its own. A session
listens through exactly one of three doors: the OS default path, a chosen
`Mel_AudioIn` (virtual devices included), or PCM the app feeds; door
violations and capability gaps fail loud, never a silent fall-back
(MEL-CODE-007).

Provider-plugin shaped like `audioin`: host OS engines are provider 0
(`mel_stt__register_host_providers`), whisper.cpp/cloud providers register
identically. Lowering names every loss (partials, vocabulary, punctuation,
profanity); `require_on_device` is functional, never best-effort. Consent is
future-shaped and composed: recognition consent from providers, microphone
consent from `audioin`, most restrictive wins — fed sessions skip consent
entirely. `on_complete` fires exactly once behind an atomic resolved guard;
graceful stop drains where the provider can, and is synthesized as abort
(warned) where it cannot. See `spec.md`.

Dependencies: `core`, `allocator`, `collection`, `string`, `log`, `thread`,
`future`, `executor`, `audioin`.
