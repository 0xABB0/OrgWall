# compress — lossless data compression

Namespace `<compress/...>`, symbol prefix `mel_compress_` (`mel_zip_` for archives).

## Shape

One folder, `modules/compress/`, whose `build.c` declares one core library and one library
per codec, so an app links only the codecs it asks for (MEL-ENGINE-III):

- `compress` — codec interface, registry, format sniffing, one-shot helpers, streaming pump.
- `compress-rle` — in-house run-length codec; zero third-party bytes, the reference
  implementation of the codec contract.
- `compress-deflate` — `deflate` (zlib container) and `gzip` codecs, backed by
  `third-party/miniz`.
- `compress-lz4` — LZ4 frame codec, backed by `third-party/lz4`.
- `compress-zstd` — Zstandard codec, backed by `third-party/zstd`.
- `compress-brotli` — Brotli codec, backed by `third-party/brotli`.
- `compress-zip` — `.zip` archive read/write (container, not a stream codec), backed by
  `third-party/miniz`.

Backend selection is **runtime coexistence, not build-time choice**: unlike `gpu`, a single
process legitimately wants every codec at once. No `Mel_When` axis; each codec is its own
link target.

## Status

`Mel_Compress_Status` is a `u32` bitfield in the house style (severity in bits 0–1, flags
above): `MEL_COMPRESS_OK/WARNED/ERROR`, flags `CORRUPT`, `TRUNCATED`, `NO_MEMORY`,
`BAD_LEVEL`, `UNKNOWN_FORMAT`, `OUTPUT_FULL`. Predicates `mel_compress_failed/ok`.
No enums (MEL-CODE-001).

## Codec contract

`Mel_Compress_Codec` is a value struct of metadata + function pointers:

```c
struct Mel_Compress_Codec
{
    str8  id;                            // "deflate", "gzip", "lz4", "zstd", "brotli", "rle"
    str8  ext;                           // canonical file extension, no dot
    u32   level_min, level_max, level_default;
    bool  (*sniff)(str8 head);           // magic-byte detection; NULL when format has no magic (brotli)
    usize (*bound)(usize src_len, u32 level);
    Mel_Compress_Stream* (*begin)(Mel_Compress_Begin begin);   // direction + level + alloc
    Mel_Compress_Step    (*step)(Mel_Compress_Stream*, str8 in, bool in_last, u8* out, usize out_cap);
    void                 (*end)(Mel_Compress_Stream*);
};
```

- Every codec getter is `const Mel_Compress_Codec* mel_compress_<id>(void)` in its own
  library; the declaration lives in the core headers, the symbol in the codec library, so an
  unlinked codec fails loudly at link time.
- `level` is **mandatory** at `begin` for compression; out-of-range is `BAD_LEVEL` error,
  never clamped silently (MEL-CODE-007). Callers wanting the canonical level read
  `level_default` explicitly.
- All allocation flows through the `Mel_Alloc*` given at `begin` (MEL-CODE-003); every
  third-party backend is wired to it via its custom-alloc hook. No hidden `malloc`.

## Streaming pump

The primitive is a synchronous, chunk-granular step — no threads, no callbacks, no hidden
buffering — precisely so a stackless `coro` body can drive it and suspend between chunks:

```c
typedef struct
{
    usize               in_consumed;
    usize               out_produced;
    bool                finished;
    Mel_Compress_Status status;
} Mel_Compress_Step;
```

The caller owns both buffers, calls `step` with the next input slice (`in_last` marks the
final one) and an output window, and loops until `finished`. `OUTPUT_FULL` is a non-error
flag meaning "call again with more output space".

## One-shot helpers

`mel_compress(codec, in, opt)` / `mel_decompress(codec, in, opt)` return
`Mel_Compress_Result { u8* data; usize len; Mel_Compress_Status status; }`, built on the
pump with a growable output buffer on `opt.alloc`.

## Registry & sniffing

Explicit registration — the app registers what it linked, nothing self-registers:

```c
mel_compress_registry_init(alloc);
mel_compress_register(mel_compress_zstd());
const Mel_Compress_Codec* c = mel_compress_find(S8("zstd"));
const Mel_Compress_Codec* d = mel_compress_sniff(head);        // first registered codec whose sniff() accepts
const Mel_Compress_Codec* e = mel_compress_for_ext(S8("br"));  // extension fallback for magic-less formats
```

Brotli has no magic bytes; sniffing falls through to extension matching. `sniff` returning
NULL is reported as `UNKNOWN_FORMAT`, never guessed (MEL-CODE-007).

## Zip archives

`<compress/zip.h>`: memory-backed reader (`open`, `entry_count`, `entry_at`, `extract`) and
writer (`create`, `add`, `finish`), entries as `{ str8 name; u64 size, csize; u32 crc; bool dir; }`
held in `Mel_Array`. Lives in `compress-zip`; depends on miniz's `mz_zip` with allocator
hooks wired.

## Third-party vendoring

`third-party/miniz` (zlib/deflate + zip, amalgamated pair), `third-party/lz4` (core + HC +
frame + xxhash), `third-party/zstd` (upstream `lib/` common+compress+decompress),
`third-party/brotli` (upstream `c/` common+dec+enc + include). Each is a standard discovered
library target with `MEL_PUBLIC` includes; warnings silenced privately (`-w`) as for `stb`.
All compile as plain C99+ on every platform including wasm.

## Failure modes considered

- **Corrupt input**: every decoder path maps backend errors to `CORRUPT`/`TRUNCATED`;
  round-trip tests include truncated and bit-flipped streams.
- **Empty input**: legal; produces a valid empty stream both directions.
- **Huge bound**: `bound()` is advisory for one-shot sizing; the pump never requires the
  whole output to fit (OUTPUT_FULL loop), so bounded memory works for any input size.
- **Allocator failure**: backends' alloc hooks return NULL through to `NO_MEMORY` error.
- **Magic-less formats**: explicit `UNKNOWN_FORMAT` instead of a guessed codec.
- **wasm**: no fibers there; nothing in this module suspends or threads — the demo app's
  concurrency comes from `coro` continuations, whose generated state machines are plain C.

## Demo app — `apps/compress-lab`

GUI app (`mel_subsystem "gui"`), runnable on macos/ios/linux/android/win32/wasm:

- Pick a file (`dialog`; falls back to a built-in sample buffer where picking is denied),
  pick codec + level, compress or decompress; decompress auto-detects via sniff + extension.
- The work runs as **`coro` continuations** (`*.coro.h` + `coro-gen` codegen): the body
  loops the streaming pump chunk-by-chunk and yields a progress value; the vat tick resumes
  the live frame within a per-frame time budget, so the UI never blocks — and the same
  state machine runs on wasm where fibers cannot (`fiber` is asm, no wasm port).
- "Race" mode: one continuation benchmarks every registered codec over the same input,
  yielding per-codec progress; results table with ratio + throughput bars.
- Output saved via save dialog (or offered as download on wasm).

## Tests

Per-codec round-trip (`empty`, `1 byte`, repetitive, text, incompressible random), pump with
1-byte output windows, sniff/extension resolution, corrupt/truncated rejection, zip
write→read round-trip, growable one-shot equality with pump output.
