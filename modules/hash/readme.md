# hash

Non-cryptographic hash functions. Depends on `core` only.

| Header            | Functions                                              | Use                                                          |
| ----------------- | ------------------------------------------------------ | ------------------------------------------------------------ |
| `<hash/xxh.h>`    | `mel_xxh64`, `mel_xxh3_64`, `mel_xxh3_64_seeded`        | default fast 64-bit hashing (hashmaps, content fingerprints) |
| `<hash/fnv.h>`    | `mel_fnv1a32`, `mel_fnv1a64`                            | tiny inputs, compile-time-friendly, interop with FNV users   |
| `<hash/crc32.h>`  | `mel_crc32[_update]`, `mel_crc32c[_update]`             | file formats, network checksums; `_update` chains buffers    |
| `<hash/murmur3.h>`| `mel_murmur3_32`                                        | interop with the many systems keyed on Murmur3               |
| `<hash/siphash.h>`| `mel_siphash24`                                         | DoS-resistant keyed hashing for attacker-controlled keys     |

SipHash-2-4 is keyed (128-bit key as `k0`,`k1`) and the only one here safe against hash-flooding; the rest must not be fed untrusted keys where collisions are exploitable. None of these are cryptographic digests.

Tests: `./nob test hash` (reference vectors per function).
