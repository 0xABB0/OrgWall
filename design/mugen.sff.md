# MUGEN SFF Loader

## Goal

Parse MUGEN `.sff` sprite files and produce a `Mel_Spritesheet` + GPU texture atlas.
The game code works with standard engine types (`Mel_Spritesheet`, `Mel_AnimationPlayer`, `Mel_SpriteFrame`) — no MUGEN-specific runtime types leak out.

## File Format (V1)

Binary file. All values little-endian.

**Header (32 bytes):**
- `char[12]` magic: `"ElecbyteSpr\0"`
- `u8[4]` version (verlo3, verlo2, verlo1, verhi)
- `u32` dummy
- `u32` number_of_sprites
- `u32` first_sprite_header_offset
- `u32` dummy
- Remaining bytes up to offset 512: title string (ASCII, informational)

**Per-Sprite Header (32 bytes, linked list starting at first_sprite_header_offset):**
- `u32` next_header_offset (absolute file position of next sprite header — linked list, NOT fixed stride)
- `u32` data_size (0 = linked sprite, references previous)
- `i16[2]` offset (x, y — axis offset for positioning)
- `u16` group
- `u16` number
- `u16` link_index (index of sprite to copy if data_size == 0)
- `u8` shared_palette flag
- PCX data follows immediately after each header (at header_offset + 32)

**Pixel Data:**
- PCX format: 8-bit paletted, RLE compressed
- PCX header at data_offset gives dimensions (rect[0..3] at offset+4)
- Palette: 768 bytes (256 * RGB) located before sprite data

**Palette (.act files):**
- Raw 768 bytes: 256 entries of RGB (3 bytes each)
- Index 0 is transparent

## V2 (future)

28-byte sprite headers, different layout, additional compression formats (RLE8, RLE5, LZ5, embedded PNG).
Separate palette table. Will be added when we encounter a V2 character.

## Architecture

### Loading Flow

1. `mel_vfs_read_file_alloc(vfs, path, &size, alloc)` — read entire SFF into memory
2. Parse file header, validate magic + version
3. Walk sprite headers, decompress each PCX sprite to 8-bit indexed pixels
4. Apply palette to produce RGBA32 pixels (index 0 = transparent)
5. Pack all RGBA32 sprites into a texture atlas (simple row packer)
6. Fill `Mel_Spritesheet`:
   - Each sprite -> `Mel_SpriteFrame` with atlas x/y/w/h and offsets
   - `texture_width` / `texture_height` from atlas dimensions
7. Upload atlas to GPU via `mel_gpu_texture_*`
8. Free intermediate CPU buffers

### Sprite Lookup

MUGEN indexes sprites by `[group, number]` pair. The spritesheet stores frames linearly.
A lookup table maps `[group, number]` -> frame index. This is needed by the AIR parser later.

### API

```c
typedef struct {
    u16 group;
    u16 number;
    u32 frame_index;
} Mugen_Sff_Entry;

typedef struct {
    Mel_Spritesheet sheet;
    Mugen_Sff_Entry* entries;
    u32 entry_count;
    u8* atlas_pixels;
    u32 atlas_width;
    u32 atlas_height;
} Mugen_Sff;

bool mugen_sff_load(Mugen_Sff* sff, Mel_Vfs* vfs, str8 path, const Mel_Alloc* alloc);
u32  mugen_sff_find_frame(Mugen_Sff* sff, u16 group, u16 number);
void mugen_sff_shutdown(Mugen_Sff* sff, const Mel_Alloc* alloc);
```

`Mugen_Sff` wraps `Mel_Spritesheet` (by value) and adds the group/number lookup.
The `atlas_pixels` field holds CPU-side RGBA data until the game uploads it to GPU, then can be freed.

### Files

- `demos/street-carlos/mugen_sff.h`
- `demos/street-carlos/mugen_sff.c`

Game-local. May move to engine if multiple demos need it.

## AIR Integration (next step)

The AIR parser will read `.air` files and populate `Mel_Animation` entries on the `Mugen_Sff.sheet`.
It uses `mugen_sff_find_frame(sff, group, number)` to convert MUGEN sprite references to frame indices.

## .act Palette Support

`.act` files are 768-byte raw RGB palettes. Loading one and re-applying it to the atlas gives palette swaps.
This is how MUGEN handles alternate colors (pal1 through pal12 in the .def file).
