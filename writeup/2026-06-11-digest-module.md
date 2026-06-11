# 2026-06-11 — digest module (cryptographic hashing)

## Work done

New `modules/digest/`: cryptographic hash functions, mirroring the shape of `modules/hash/` (per-algorithm header + source, one-shot + streaming state with non-destructive `final`, vectors test target).

Algorithms, all implemented from the primary specifications, portable C, no dependencies beyond `core`:

- MD5, SHA-1 (legacy interop, flagged as broken in the readme)
- SHA-2: SHA-224/256/384/512, SHA-512/224, SHA-512/256 (shared state per word width)
- SHA-3: SHA3-224/256/384/512 plus SHAKE128/256 XOF with stateful `mel_shake_squeeze`
- RIPEMD-160, SM3
- BLAKE2b/BLAKE2s with keyed mode and variable output length
- BLAKE3 with keyed and derive-key modes, arbitrary-length (XOF) output, full chunk-tree streaming

Testing (`./nob test digest-vectors`, 36 tests, also run in `--release`):
- Vectors for every fixed-output algorithm generated from Python `hashlib` (OpenSSL-backed) over 17 message lengths of the same fill pattern the hash module uses; generator script kept out of the repo, output checked in as `test/digest_vectors.h`.
- BLAKE3 checked against the official 35-case suite from the BLAKE3 repository (hash/keyed/derive × 131-byte XOF output, inputs up to 100 KiB exercising the tree).
- Every streaming state checked chunked-vs-one-shot with irregular chunk sizes; non-destructive `final` checked by finalizing twice.

Worktree note: the worktree branched from `origin/main`, which was behind local `main` (missing the recent hash-module commits); reset the branch onto local `main` before starting.

## Kludges

- None knowingly. Two judgement calls worth surfacing:
  - Digest structs return fixed byte arrays (`u8 bytes[32]`); these are algorithm-defined output sizes, not capacity maxima, so I read MEL-CODE-002 as not applying. Same for the BLAKE3 CV stack (`[54][8]` words — the algorithm's hard depth bound for 2^64-byte inputs).
  - `mel_blake2b/2s` and the SHAKE/BLAKE3 one-shots write through a caller `out` pointer instead of returning a struct because output length is caller-chosen. Mixed return-style within one module is deliberate, not drift.
- Test vectors are generated (by a throwaway Python script) rather than hand-copied from RFCs; the generator is not checked in, only its output. Regenerating requires rerunning hashlib — acceptable since the vectors never change, but noting it.

## CLAUDE.md suggestions

- None.

## Suggestions

- HMAC/HKDF are the natural next layer (see `modules/digest/todo.md`); several future modules (HTTP signing, JWT) will want them.
- A constant-time comparison helper should land before anyone verifies a MAC with `memcmp`.
- `modules/hash/readme.md` could point at `digest` now that "none of these are cryptographic digests" has a counterpart module.
