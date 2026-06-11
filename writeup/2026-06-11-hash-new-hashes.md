# 2026-06-11 — hash: new hash functions

## Work done

The hash module carried only xxhash (`mel_xxh64`, `mel_xxh3_64[_seeded]`). Added four families, each as its own header/source pair in the existing style (`mel__` internals, `memcpy` reads, no comments):

- `<hash/fnv.h>` — FNV-1a 32/64 (`mel_fnv1a32`, `mel_fnv1a64`).
- `<hash/crc32.h>` — CRC-32 (zlib poly) and CRC-32C (Castagnoli), each with a one-shot and an `_update` chaining form sharing one table-driven core.
- `<hash/murmur3.h>` — Murmur3 x86 32-bit (`mel_murmur3_32`).
- `<hash/siphash.h>` — SipHash-2-4 keyed 64-bit (`mel_siphash24`), the DoS-resistant option.

Added the module's missing `readme.md`, and a `hash-vectors` test target (the bare name `hash` collides with the library artifact in ninja) with reference vectors per function: FNV spec vectors, CRC check value `0xCBF43926`/`0xE3069283` plus chaining, Murmur3 SMHasher vectors, SipHash paper vectors (key 00..0f, msg 00..len−1), and an xxh64 vector + xxh3 seed-0 equivalence since the module previously had no tests at all. 10/10 pass on macos-debug.

## Work done (second pass)

Gabbo approved the follow-up set:

- `mel_xxh3_128[_seeded]` — XXH3 128-bit, sharing the existing accumulation core (`mel__xxh3_hash_long` split into accum + merge so both widths reuse it).
- `<hash/mix.h>` — header-only `mel_hash_mix32` (Murmur3 fmix32), `mel_hash_mix64` (splitmix64 finalizer), `mel_hash_combine64` (boost-style combine with the 64-bit finalizer). `collection/hashmap.c` u64/ptr key hashing folded over from full xxh64 to `mel_hash_mix64`; string keys unchanged.
- Streaming xxh3 — `Mel_Xxh3_State` with `init[_seeded]/update/final_64/final_128`, faithful to the reference (256-byte internal buffer, block-wrapping stripe consumption, non-destructive final, both widths from one state).

Verification: Python `xxhash` 3.6.0 used as oracle to generate `test/xxh3_vectors.h` — 38 vectors (19 lengths covering every code path: 0..16 sub-paths, 17..128, 129..240, single/multi block long paths; seeds 0 and nonzero) checked for 64-bit, 128-bit, and streaming (5 chunk sizes each). Mixer outputs pinned to oracle-computed constants (incl. the canonical splitmix64 step 0xE220A8397B1DCDAF). `hash-vectors` 14/14, `collection-slotmap` 3/3, `collection-mpsc` 11/11.

## Kludges

- None in the shipped code. Noted for transparency: the two 256-entry CRC tables were emitted by a throwaway Python script during the session; the generator is not committed, only its output. The tables are protocol constants (polynomials fixed by the CRC-32/CRC-32C standards), so regeneration should never be needed.
- Murmur3 tail and SipHash tail use intentional switch fallthrough, as in the reference implementations; compiles warning-free here but a future `-Wimplicit-fallthrough` config would flag it.
- Second pass: `test/xxh3_vectors.h` is oracle-generated (python xxhash 3.6.0), generator not committed — regenerating requires that package, but the vectors are frozen reference values that never change.
- `mel_hashmap_hash_u64/ptr` outputs changed (xxh64 → splitmix64 finalizer); hashmap hashes are in-memory only, nothing persists them, so no migration concern.

## CLAUDE.md suggestions

- None.

## Suggestions

- `mel_crc32c` has hardware paths (SSE4.2 `crc32`, ARMv8 CRC extensions) worth wiring under the existing axis-selector machinery if checksumming ever shows up in a profile (MEL-CODE-006 before adding it).
- If a cryptographic digest (SHA-256) is ever needed (content addressing, protocol handshakes), it belongs in a separate module — different threat model than this one.
- `collection/hashmap.c` hardcodes `mel_xxh64`; with SipHash now available, a keyed-hash option for maps over attacker-controlled keys becomes possible.
