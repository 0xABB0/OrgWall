# digest

Cryptographic hash functions. Depends on `core` only. The non-cryptographic counterpart is `hash`.

Every algorithm offers a one-shot function and a streaming state (`init`/`update`/`final`); `final` is non-destructive, so a running digest can be sampled mid-stream and continued.

| Header                | Functions                                                                  | Use                                                            |
| --------------------- | -------------------------------------------------------------------------- | --------------------------------------------------------------- |
| `<digest/sha2.h>`     | `mel_sha224/256/384/512`, `mel_sha512_224/256` (+ streaming)               | the default modern digest; interop with TLS, signatures, content addressing |
| `<digest/sha3.h>`     | `mel_sha3_224/256/384/512` (+ streaming)                                   | Keccak-based FIPS 202 digests                                   |
| `<digest/sha3.h>`     | `mel_shake128/256`, `Mel_Shake_State` with `mel_shake_squeeze`             | XOF: arbitrary-length output, repeated squeezing                |
| `<digest/blake2.h>`   | `mel_blake2b` (≤64B out, ≤64B key), `mel_blake2s` (≤32B out, ≤32B key)     | fast software digest, optional keying (MAC), RFC 7693 interop   |
| `<digest/blake3.h>`   | `mel_blake3`, `mel_blake3_keyed`, `mel_blake3_derive_key` (+ streaming, all XOF) | fastest here; hashing, keyed MAC and KDF in one primitive |
| `<digest/ripemd160.h>`| `mel_ripemd160` (+ streaming)                                              | Bitcoin addresses, PGP fingerprints interop                     |
| `<digest/sm3.h>`      | `mel_sm3` (+ streaming)                                                    | Chinese national standard (GB/T 32905) interop                  |
| `<digest/sha1.h>`     | `mel_sha1` (+ streaming)                                                   | legacy interop only (git objects, old protocols); collisions exist |
| `<digest/md5.h>`      | `mel_md5` (+ streaming)                                                    | legacy interop only (checksums, old formats); broken            |

MD5 and SHA-1 are cryptographically broken; they exist for interoperating with formats that demand them, never for new designs. BLAKE2 keyed mode and BLAKE3 keyed/derive modes are the only keyed constructions here; generic HMAC lives in no module yet (see todo).

All states live on the stack and allocate nothing.

Tests: `./nob test digest-vectors` — every fixed-output algorithm is checked against vectors generated from authoritative references (Python `hashlib`/OpenSSL) over a shared fill pattern; BLAKE3 runs the official 35-case test-vector suite (hash/keyed/derive, 131-byte XOF output); every streaming state is checked chunked-vs-one-shot.
