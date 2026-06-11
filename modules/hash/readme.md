# hash

Non-cryptographic hash functions. Depends on `core` only.

| Header            | Functions                                              | Use                                                          |
| ----------------- | ------------------------------------------------------ | ------------------------------------------------------------ |
| `<hash/xxh.h>`    | `mel_xxh64`, `mel_xxh3_64[_seeded]`, `mel_xxh3_128[_seeded]` | default fast hashing (hashmaps, content fingerprints); 128-bit for low-collision fingerprints |
| `<hash/xxh.h>`    | `Mel_Xxh3_State`: `mel_xxh3_init[_seeded]`, `mel_xxh3_update`, `mel_xxh3_final_64`, `mel_xxh3_final_128` | incremental xxh3 over chunked input (files, streams); final is non-destructive, both widths from one state |
| `<hash/mix.h>`    | `mel_hash_mix32`, `mel_hash_mix64`, `mel_hash_combine64` | header-only integer/pointer mixers and hash combining        |
| `<hash/fnv.h>`    | `mel_fnv1a32`, `mel_fnv1a64`                            | tiny inputs, compile-time-friendly, interop with FNV users   |
| `<hash/crc32.h>`  | `mel_crc32[_update]`, `mel_crc32c[_update]`             | file formats, network checksums; `_update` chains buffers    |
| `<hash/murmur3.h>`| `mel_murmur3_32`                                        | interop with the many systems keyed on Murmur3               |
| `<hash/siphash.h>`| `mel_siphash24`                                         | DoS-resistant keyed hashing for attacker-controlled keys     |

SipHash-2-4 is keyed (128-bit key as `k0`,`k1`) and the only one here safe against hash-flooding; the rest must not be fed untrusted keys where collisions are exploitable. None of these are cryptographic digests; for those see `digest`.

Tests: `./nob test hash` (reference vectors per function).
