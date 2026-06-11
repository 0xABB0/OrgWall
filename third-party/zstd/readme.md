# zstd

Zstandard 1.5.7 `lib/` sources (https://github.com/facebook/zstd), BSD-3-Clause:
`common/`, `compress/`, `decompress/` only (no legacy, no dictBuilder). The x86-64
assembly fast path is excluded (`ZSTD_DISABLE_ASM`) so the same C sources build on
every platform including wasm. Consumed by `modules/compress` for the `zstd` codec.
