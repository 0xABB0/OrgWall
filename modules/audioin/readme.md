# audioin

Audio input device identity: enumerate inputs across providers, describe
them, name the system default, own input consent and input gain, publish
app-created inputs. No audio is read here — `audiocapture` turns a
`Mel_AudioIn` into a stream. Twin of `audioout`.

The host OS is provider 0; anyone registers more via
`<audioin/provider.h>`. Published inputs live on a built-in virtual
provider; OS-wide visibility is reported honestly (`WARN_LOCAL_ONLY` where
the platform has no publish path). Host platform providers are not yet
implemented — registration logs loudly and only registered/published
providers enumerate. See `spec.md` for the full contract.

Dependencies: `core`, `allocator`, `string`, `collection`, `future`,
`executor`, `event`, `pcm`, `log`.
