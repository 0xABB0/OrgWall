# Compress module + compress-lab demo app

## Work done

**`modules/compress`** — lossless compression for the framework. One folder, several
link targets so apps pay only for the codecs they ask for (MEL-ENGINE-III):

- `compress` — codec contract (`Mel_Compress_Codec` vtable), explicit runtime registry
  with magic-byte sniffing + extension fallback, one-shot helpers over a growable pump.
  Status is the house-style `u32` bitfield; no enums; `level` is mandatory and validated,
  never clamped (MEL-CODE-007); every allocation flows through the `Mel_Alloc*` given at
  `begin` — all four third-party backends wired through their custom-alloc hooks.
- `compress-rle` — in-house PackBits-style codec (magic `MRL1`, explicit EOS op);
  the dependency-free reference implementation of the contract.
- `compress-deflate` — `deflate` (zlib container) and `gzip` codecs over
  `third-party/miniz`; gzip header/trailer is hand-framed around raw deflate with
  incremental, chunk-boundary-safe parsing (FEXTRA/FNAME/FCOMMENT/FHCRC all skipped
  correctly; CRC32 + ISIZE verified on decode).
- `compress-lz4` (frame format), `compress-zstd`, `compress-brotli` — streaming wrappers.
- `compress-zip` — `.zip` archive reader/writer over miniz `mz_zip`, memory-backed.

The streaming primitive is a synchronous chunk-granular `step` (input slice in, output
window out, repeat until `finished`) — designed so a stackless `coro` continuation can
drive it and suspend between chunks. `OUTPUT_FULL` is a non-error flag; truncated and
corrupt inputs fail loudly with dedicated bits.

**Vendored** (each a discovered `third-party/` library with version + license noted in
its readme): miniz 3.0.2, lz4 1.10.0, zstd 1.5.7 (lib/ tree, `ZSTD_DISABLE_ASM` so plain
C builds everywhere incl. wasm), brotli 1.1.0.

**`apps/compress-lab`** — boot-hosted GUI demo (no `main`): open a file via `dialog`
(or generate sample data), pick codec + level (slider maps onto the codec's true range),
compress / decompress (auto-detect via sniff → extension; zip archives are listed and the
largest entry extracted), save the output via save dialog. The pipeline runs as **`coro`
continuations** (`job.coro.h`, codegen'd by `coro-gen`): `lab_pump` loops the streaming
pump and yields progress; `lab_race_run` benchmarks every registered codec by
`mel_coro_await`ing a child `lab_pump` frame per codec — composition with yield
forwarding, exactly the relay pattern. A vat tick resumes the live frame within an 8 ms
budget per 16 ms tick, so the UI never blocks; the tick auto-pauses (returns false) when
idle and is re-armed via `mel_vat_tick_set_interval`. Coro was chosen over `fiber`
deliberately: fibers are assembly with no wasm port; the coro state machine is plain C
and runs on every platform.

`--smoke` runs headless: per-codec coro-driven roundtrip with byte-exact verification,
then the race; verdict on stderr + exit code. This is the cross-platform proof harness.

**Verification**

- macos: `compress-roundtrip` 25/25, `compress-zip-test` 3/3 (roundtrips over empty/1B/
  text/repetitive/random payloads at min/default/max levels, 3-byte-in/7-byte-out pump
  windows, truncation + bad-level rejection, registry/sniff/ext, zip write→read).
  GUI launch verified alive; `--smoke` ALL OK.
- wasm: builds; `node compress-lab.js --smoke` → "ALL OK" (needed
  `-sALLOW_MEMORY_GROWTH=1` on the app).
- ios (simulator): builds + packages `.app`.
- android: builds + packages APK.
- linux: `compress-roundtrip` and every compress lib cross-compile and link; the *app*
  is blocked by a pre-existing `dialog` portal backend issue (`dbus/dbus.h` missing in
  the zig cross toolchain) — not introduced here.
- win32: not verified — requires push to origin + win-pilot per the documented workflow,
  and no push was requested this session. Sources are MSVC-clean upstream libraries and
  plain C.

Two real bugs found by the suite during development: the RLE decoder emitted a run before
its fill byte arrived when control/fill split across chunks; the zlib decompress path
reported TRUNCATED when output-full and input-exhausted coincided.

The design spec was drafted in `design/compress.md`, then moved to
`modules/compress/spec.md` once the module existed (MEL-SPEC-002).

## Kludges

- **Fixed-size protocol scratch** in `deflate_codec.c` (`u8 trl[8]`, `gtrl[8]`) and the
  RLE magic table: byte-exact wire-format staging, not capacity limits — but MEL-CODE-002
  is absolute, so confessing. The RLE outbox/literal buffers are allocator-allocated to
  honor the rule; their capacities (512/128) are format-derived constants.
- **`char buf[N]` snprintf scratch** throughout the app UI (same pattern as
  melody-showcase). Text formatting into stack scratch, not data structures.
- **zlib sniffing is heuristic** (CM==8 + header checksum, ~1/496 false-positive on
  random data); mitigated by registering `deflate` last. Brotli has no magic at all —
  extension-only detection, `UNKNOWN_FORMAT` otherwise, by design.
- **Zip UI reuses the race result labels** to list entries and only shows the first
  `codec_count` of them; "Decompress" on an archive extracts the largest entry only.
- **wasm smoke verdict** is read from a stderr line, not the process exit code — the
  emscripten runtime here lacks `EXIT_RUNTIME`, so `emscripten_force_exit` can't carry
  the status and stdio isn't flushed at teardown.
- **`compress-zip` is linked into the app but its writer is unused by the UI** (reader
  only); writer is covered by `compress-zip-test`.
- **Smoke not executed on ios/android** (build+package only); executing it there needs a
  simulator run hook this session didn't take on.

## CLAUDE.md suggestions (recommendations only, not applied)

- Document the third-party vendoring convention (subfolder named after the lib, upstream
  version + license stated in `readme.md`, `-w` private cflags) — four new libs followed
  an inferred pattern.
- Note that `coro-gen` requires LLVM at `/opt/homebrew/opt/llvm` (hardcoded in
  `modules/coro/build.c`) — worth stating near the build docs since any app using coro
  codegen inherits the requirement.

## Suggestions

- `mel_vat_tick_set_interval` doubling as "resume" after a pause is non-obvious; an
  explicit `mel_vat_tick_resume` would read better.
- Fix linux `dialog` portal build (vendor dbus headers or gate the backend) — it blocks
  every dialog-using app from the zig cross toolchain.
- Compress follow-ups: gzip multi-member streams, zstd dictionaries, an `fs`-level
  `mel_compress_file` convenience that pipes the pump through streams, zip writer
  streaming to disk instead of heap, and a `compress-lab` panel to *create* archives.
- The wasm CLI story (exit codes, stdio flushing for `--smoke`-style runs) deserves a
  boot-level decision.
