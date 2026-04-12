# gpu.pipeline_cache

## TODO

- Hash-only cache lookup with no deep compare on collision. The 24-byte key is three pre-computed XXH3-64 sub-hashes. If two different pipeline opts produce the same key, one overwrites the other silently. Should store the full `Mel_Gpu_Pipeline_Opt` alongside the cached pipeline and deep-compare on hash match. Probability is astronomically low but not formally correct.
