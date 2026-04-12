# texture.pool

## TODO

- The dedup key currently captures path, format, and nearest filtering, but not richer sampler state. If the pool starts exposing address modes, anisotropy, or compare sampling, those must become part of the key too.
