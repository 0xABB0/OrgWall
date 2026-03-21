# texture

## TODO

- The generic loader still assumes 8-bit RGBA decode for every file. If we start consuming HDR, BC-compressed, or higher-precision assets directly, this module should expose explicit decode/storage choices instead of flattening everything through one STB path.
