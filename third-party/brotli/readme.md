# brotli

Brotli 1.1.0 `c/` sources (https://github.com/google/brotli), MIT: `common/`, `dec/`,
`enc/` plus the public `include/brotli` headers. Consumed by `modules/compress` for the
`brotli` codec (streaming encoder/decoder, allocator hooks wired to `Mel_Alloc`).
