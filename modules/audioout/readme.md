# audioout

Audio output device identity: enumerate outputs across providers, describe
them, name the system default, own OS volume/mute, publish app-created
outputs. No audio is mixed here — `audio` binds its engine to a
`Mel_AudioOut`. Twin of `audioin` with two deliberate differences: no
consent surface, and a pull stream plane (output devices ask for frames on
their own clock).

The host OS is provider 0; virtual sinks register the same way. Published
outputs are devices others play into — the publisher's `publish_read` is
the pull clock, draining and summing every started opener. OS-wide
visibility is reported honestly (`WARN_LOCAL_ONLY` where the platform has
no publish path). See `spec.md` for the full contract.

Dependencies: `core`, `allocator`, `string`, `collection`, `executor`,
`event`, `log`.
