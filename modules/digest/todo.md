# todo

- HMAC over any digest in this module (HMAC-SHA256/512 first); needed for JWT, AWS SigV4, TLS interop.
- HKDF (RFC 5869) on top of HMAC.
- KMAC / cSHAKE on top of the Keccak sponge.
- BLAKE3 seekable output (`mel_blake3_final` at an output offset).
- SIMD lanes for BLAKE3/BLAKE2 (NEON/SSE) once profiling demands it.
- Constant-time digest comparison helper (`mel_digest_equals`) to avoid timing leaks in MAC verification.
