#include <digest/blake3.h>
#include <string.h>

#define MEL__B3_CHUNK_START         1u
#define MEL__B3_CHUNK_END           2u
#define MEL__B3_PARENT              4u
#define MEL__B3_ROOT                8u
#define MEL__B3_KEYED_HASH          16u
#define MEL__B3_DERIVE_KEY_CONTEXT  32u
#define MEL__B3_DERIVE_KEY_MATERIAL 64u

static const u32 mel__b3_iv[8] = {
    0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U, 0xA54FF53AU, 0x510E527FU, 0x9B05688CU, 0x1F83D9ABU, 0x5BE0CD19U,
};

static const u8 mel__b3_perm[16] = { 2, 6, 3, 10, 7, 0, 4, 13, 1, 11, 12, 5, 9, 14, 15, 8 };

static inline u32 mel__b3_rotr32(u32 v, int n) { return (v >> n) | (v << (32 - n)); }

static inline u32 mel__b3_read32le(const u8* p)
{
    u32 v;
    memcpy(&v, p, sizeof(v));
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    v = __builtin_bswap32(v);
#endif
    return v;
}

static inline void mel__b3_words_from_block(u32 w[16], const u8* block)
{
    for (int i = 0; i < 16; i++)
        w[i] = mel__b3_read32le(block + 4 * i);
}

static inline void mel__b3_words_from_key(u32 w[8], const u8* key)
{
    for (int i = 0; i < 8; i++)
        w[i] = mel__b3_read32le(key + 4 * i);
}

static inline void mel__b3_g(u32 v[16], int a, int b, int c, int d, u32 x, u32 y)
{
    v[a] += v[b] + x;
    v[d] = mel__b3_rotr32(v[d] ^ v[a], 16);
    v[c] += v[d];
    v[b] = mel__b3_rotr32(v[b] ^ v[c], 12);
    v[a] += v[b] + y;
    v[d] = mel__b3_rotr32(v[d] ^ v[a], 8);
    v[c] += v[d];
    v[b] = mel__b3_rotr32(v[b] ^ v[c], 7);
}

static void mel__b3_compress(u32 out[16], const u32 cv[8], const u32 block_words[16], u64 counter, u32 block_len, u32 flags)
{
    u32 v[16] = {
        cv[0], cv[1], cv[2], cv[3], cv[4], cv[5], cv[6], cv[7], mel__b3_iv[0], mel__b3_iv[1], mel__b3_iv[2], mel__b3_iv[3], (u32)counter, (u32)(counter >> 32), block_len, flags,
    };
    u32 m[16];
    memcpy(m, block_words, sizeof(m));

    for (int r = 0;; r++)
    {
        mel__b3_g(v, 0, 4, 8, 12, m[0], m[1]);
        mel__b3_g(v, 1, 5, 9, 13, m[2], m[3]);
        mel__b3_g(v, 2, 6, 10, 14, m[4], m[5]);
        mel__b3_g(v, 3, 7, 11, 15, m[6], m[7]);
        mel__b3_g(v, 0, 5, 10, 15, m[8], m[9]);
        mel__b3_g(v, 1, 6, 11, 12, m[10], m[11]);
        mel__b3_g(v, 2, 7, 8, 13, m[12], m[13]);
        mel__b3_g(v, 3, 4, 9, 14, m[14], m[15]);

        if (r == 6)
            break;

        u32 next[16];
        for (int i = 0; i < 16; i++)
            next[i] = m[mel__b3_perm[i]];
        memcpy(m, next, sizeof(m));
    }

    for (int i = 0; i < 8; i++)
    {
        out[i] = v[i] ^ v[i + 8];
        out[i + 8] = v[i + 8] ^ cv[i];
    }
}

typedef struct Mel__B3_Output
{
    u32 input_cv[8];
    u32 block_words[16];
    u64 counter;
    u32 block_len;
    u32 flags;
} Mel__B3_Output;

static void mel__b3_output_cv(const Mel__B3_Output* o, u32 cv[8])
{
    u32 full[16];
    mel__b3_compress(full, o->input_cv, o->block_words, o->counter, o->block_len, o->flags);
    memcpy(cv, full, 8 * sizeof(u32));
}

static void mel__b3_output_root(const Mel__B3_Output* o, u8* out, usize out_len)
{
    u64 block_counter = 0;
    while (out_len)
    {
        u32 full[16];
        mel__b3_compress(full, o->input_cv, o->block_words, block_counter, o->block_len, o->flags | MEL__B3_ROOT);

        usize take = out_len < 64 ? out_len : 64;
        for (usize i = 0; i < take; i++)
            out[i] = (u8)(full[i >> 2] >> (8 * (i & 3)));
        out += take;
        out_len -= take;
        block_counter++;
    }
}

static inline u32 mel__b3_start_flag(const Mel_Blake3_State* st) { return st->blocks_compressed == 0 ? MEL__B3_CHUNK_START : 0; }

static inline usize mel__b3_chunk_len(const Mel_Blake3_State* st) { return (usize)st->blocks_compressed * 64 + st->block_len; }

static void mel__b3_chunk_output(const Mel_Blake3_State* st, Mel__B3_Output* o)
{
    memcpy(o->input_cv, st->chunk_cv, sizeof(o->input_cv));
    u8 block[64];
    memcpy(block, st->block, st->block_len);
    memset(block + st->block_len, 0, 64 - st->block_len);
    mel__b3_words_from_block(o->block_words, block);
    o->counter = st->chunk_counter;
    o->block_len = (u32)st->block_len;
    o->flags = st->flags | mel__b3_start_flag(st) | MEL__B3_CHUNK_END;
}

static void mel__b3_parent_output(const u32 left[8], const u32 right[8], const u32 key[8], u32 flags, Mel__B3_Output* o)
{
    memcpy(o->input_cv, key, 8 * sizeof(u32));
    memcpy(o->block_words, left, 8 * sizeof(u32));
    memcpy(o->block_words + 8, right, 8 * sizeof(u32));
    o->counter = 0;
    o->block_len = 64;
    o->flags = MEL__B3_PARENT | flags;
}

static void mel__b3_init_with_key(Mel_Blake3_State* st, const u32 key[8], u32 flags)
{
    memcpy(st->key, key, 8 * sizeof(u32));
    memcpy(st->chunk_cv, key, 8 * sizeof(u32));
    st->cv_stack_len = 0;
    st->chunk_counter = 0;
    st->block_len = 0;
    st->blocks_compressed = 0;
    st->flags = flags;
}

void mel_blake3_init(Mel_Blake3_State* st) { mel__b3_init_with_key(st, mel__b3_iv, 0); }

void mel_blake3_init_keyed(Mel_Blake3_State* st, const u8 key[32])
{
    u32 key_words[8];
    mel__b3_words_from_key(key_words, key);
    mel__b3_init_with_key(st, key_words, MEL__B3_KEYED_HASH);
}

void mel_blake3_init_derive_key(Mel_Blake3_State* st, const void* context, usize context_len)
{
    Mel_Blake3_State ctx;
    mel__b3_init_with_key(&ctx, mel__b3_iv, MEL__B3_DERIVE_KEY_CONTEXT);
    mel_blake3_update(&ctx, context, context_len);
    u8 context_key[32];
    mel_blake3_final(&ctx, context_key, sizeof(context_key));

    u32 key_words[8];
    mel__b3_words_from_key(key_words, context_key);
    mel__b3_init_with_key(st, key_words, MEL__B3_DERIVE_KEY_MATERIAL);
}

static void mel__b3_push_chunk_cv(Mel_Blake3_State* st, const u32 cv[8], u64 total_chunks)
{
    u32 merged[8];
    memcpy(merged, cv, sizeof(merged));

    while ((total_chunks & 1) == 0)
    {
        Mel__B3_Output o;
        st->cv_stack_len--;
        mel__b3_parent_output(st->cv_stack[st->cv_stack_len], merged, st->key, st->flags, &o);
        mel__b3_output_cv(&o, merged);
        total_chunks >>= 1;
    }

    memcpy(st->cv_stack[st->cv_stack_len], merged, sizeof(merged));
    st->cv_stack_len++;
}

void mel_blake3_update(Mel_Blake3_State* st, const void* data, usize len)
{
    const u8* p = (const u8*)data;

    while (len)
    {
        if (mel__b3_chunk_len(st) == 1024)
        {
            Mel__B3_Output o;
            mel__b3_chunk_output(st, &o);
            u32 cv[8];
            mel__b3_output_cv(&o, cv);

            u64 total_chunks = st->chunk_counter + 1;
            mel__b3_push_chunk_cv(st, cv, total_chunks);

            memcpy(st->chunk_cv, st->key, sizeof(st->chunk_cv));
            st->chunk_counter = total_chunks;
            st->block_len = 0;
            st->blocks_compressed = 0;
        }

        if (st->block_len == 64)
        {
            u32 block_words[16];
            mel__b3_words_from_block(block_words, st->block);
            u32 full[16];
            mel__b3_compress(full, st->chunk_cv, block_words, st->chunk_counter, 64, st->flags | mel__b3_start_flag(st));
            memcpy(st->chunk_cv, full, 8 * sizeof(u32));
            st->blocks_compressed++;
            st->block_len = 0;
        }

        usize take = 64 - st->block_len;
        if (take > len)
            take = len;
        memcpy(st->block + st->block_len, p, take);
        st->block_len += take;
        p += take;
        len -= take;
    }
}

void mel_blake3_final(const Mel_Blake3_State* st, void* out, usize out_len)
{
    Mel__B3_Output o;
    mel__b3_chunk_output(st, &o);

    usize remaining = st->cv_stack_len;
    while (remaining)
    {
        u32 cv[8];
        mel__b3_output_cv(&o, cv);
        remaining--;
        mel__b3_parent_output(st->cv_stack[remaining], cv, st->key, st->flags, &o);
    }

    mel__b3_output_root(&o, (u8*)out, out_len);
}

void mel_blake3(const void* data, usize len, void* out, usize out_len)
{
    Mel_Blake3_State st;
    mel_blake3_init(&st);
    mel_blake3_update(&st, data, len);
    mel_blake3_final(&st, out, out_len);
}

void mel_blake3_keyed(const u8 key[32], const void* data, usize len, void* out, usize out_len)
{
    Mel_Blake3_State st;
    mel_blake3_init_keyed(&st, key);
    mel_blake3_update(&st, data, len);
    mel_blake3_final(&st, out, out_len);
}

void mel_blake3_derive_key(const void* context, usize context_len, const void* material, usize material_len, void* out, usize out_len)
{
    Mel_Blake3_State st;
    mel_blake3_init_derive_key(&st, context, context_len);
    mel_blake3_update(&st, material, material_len);
    mel_blake3_final(&st, out, out_len);
}
