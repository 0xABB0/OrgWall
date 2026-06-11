# compress

Lossless data compression: a codec contract (`<compress/codec.h>`), an explicit runtime
registry with magic-byte sniffing and extension fallback (`<compress/registry.h>`),
one-shot helpers (`<compress/compress.h>`), and `.zip` archive read/write
(`<compress/zip.h>`).

One folder, several link targets — an app links only the codecs it asks for:

| target | codecs | backend |
|---|---|---|
| `compress` | — (contract, registry, one-shot) | — |
| `compress-rle` | `rle` | in-house (PackBits-style + magic + EOS) |
| `compress-deflate` | `deflate` (zlib), `gzip` | `third-party/miniz` |
| `compress-lz4` | `lz4` (frame) | `third-party/lz4` |
| `compress-zstd` | `zstd` | `third-party/zstd` |
| `compress-brotli` | `brotli` | `third-party/brotli` |
| `compress-zip` | zip archives | `third-party/miniz` |

The streaming primitive is a synchronous chunk-granular `step` (consume an input slice,
fill an output window, repeat until `finished`), so a stackless `coro` continuation can
drive it and suspend between chunks on every platform, wasm included. All allocation
flows through the `Mel_Alloc*` given at `begin`; every backend's custom-alloc hook is
wired to it. Compression `level` is mandatory and validated — read `level_default` off
the codec for the canonical choice.

Tests: `./nob test compress-roundtrip`, `./nob test compress-zip-test`.
Demo: `apps/compress-lab` (`--smoke` runs the headless coro-driven roundtrip + race).

Spec: [spec.md](spec.md).
