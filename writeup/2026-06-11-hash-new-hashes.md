# 2026-06-11 — hash: new hash functions

## Work done

The hash module carried only xxhash (`mel_xxh64`, `mel_xxh3_64[_seeded]`). Added four families, each as its own header/source pair in the existing style (`mel__` internals, `memcpy` reads, no comments):

- `<hash/fnv.h>` — FNV-1a 32/64 (`mel_fnv1a32`, `mel_fnv1a64`).
- `<hash/crc32.h>` — CRC-32 (zlib poly) and CRC-32C (Castagnoli), each with a one-shot and an `_update` chaining form sharing one table-driven core.
- `<hash/murmur3.h>` — Murmur3 x86 32-bit (`mel_murmur3_32`).
- `<hash/siphash.h>` — SipHash-2-4 keyed 64-bit (`mel_siphash24`), the DoS-resistant option.

Added the module's missing `readme.md`, and a `hash-vectors` test target (the bare name `hash` collides with the library artifact in ninja) with reference vectors per function: FNV spec vectors, CRC check value `0xCBF43926`/`0xE3069283` plus chaining, Murmur3 SMHasher vectors, SipHash paper vectors (key 00..0f, msg 00..len−1), and an xxh64 vector + xxh3 seed-0 equivalence since the module previously had no tests at all. 10/10 pass on macos-debug.

## Kludges

- None in the shipped code. Noted for transparency: the two 256-entry CRC tables were emitted by a throwaway Python script during the session; the generator is not committed, only its output. The tables are protocol constants (polynomials fixed by the CRC-32/CRC-32C standards), so regeneration should never be needed.
- Murmur3 tail and SipHash tail use intentional switch fallthrough, as in the reference implementations; compiles warning-free here but a future `-Wimplicit-fallthrough` config would flag it.

## CLAUDE.md suggestions

- None.

## Suggestions

- `mel_crc32c` has hardware paths (SSE4.2 `crc32`, ARMv8 CRC extensions) worth wiring under the existing axis-selector machinery if checksumming ever shows up in a profile (MEL-CODE-006 before adding it).
- If a cryptographic digest (SHA-256) is ever needed (content addressing, protocol handshakes), it belongs in a separate module — different threat model than this one.
- `collection/hashmap.c` hardcodes `mel_xxh64`; with SipHash now available, a keyed-hash option for maps over attacker-controlled keys becomes possible.
