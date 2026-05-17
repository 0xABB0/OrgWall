#include <string/str8.h>

#include <allocator/allocator.h>

// TODO: use a linked list (<collection.linkedlist/llist.h>) instead of a dynamic array
// TODO: keep track of the total size of the resulting string so that we can allocate the result in one go

typedef struct Mel_String_Builder Mel_String_Builder;

struct Mel_String_Builder {
    Mel_Array(str8) parts;
};

static inline bool mel_sb_init(Mel_String_Builder* sb, usize slots, Mel_Alloc* alloc) {
    mel_array_init(&sb->parts, alloc);
    mel_array_reserve(&sb->parts, slots);

    if (sb->parts.capacity != slots) {
        mel_array_free(&sb->parts);
        return false;
    }

    return true;
}

static inline void mel_sb_reset(Mel_String_Builder* sb) {
    for (usize i = 0; i < sb->parts.count; i++) {
        mel_dealloc(sb->parts.allocator, sb->parts.items[i].data);
    }
    mel_array_free(&sb->parts);
}

static inline bool mel_sb_append_buf(Mel_String_Builder* sb, void* buf, size size) {
    // todo
    return false;
}

static inline bool mel_sb_append_str8(Mel_String_Builder* sb, str8* str) {
    // todo
    return false;
}

static inline bool mel_sb_append_cstr(Mel_String_Builder* sb, const char* str) {
    // todo
    return false;
}

static inline bool mel_sb_append_null(Mel_String_Builder* sb) {
    // todo
    return false;
}

static inline str8 mel_sb_to_str8(Mel_String_Builder* sb, Mel_Alloc* alloc) {
    // todo
    str8 result = {0};
    return result;
}

// typedef Mel_Array(char) Mel_String_Builder;

// #define mel_sb_append_buf(sb, buf, size)                            \
//     do {                                                            \
//         if ((sb)->allocator == NULL) mel_array_init((sb), mel_alloc_heap()); \
//         usize __mel_sb_n = (size);                                  \
//         mel_array_reserve((sb), (sb)->count + __mel_sb_n);           \
//         memcpy((sb)->items + (sb)->count, (buf), __mel_sb_n);        \
//         (sb)->count += __mel_sb_n;                                   \
//     } while (0)

// #define mel_sb_append_cstr(sb, cstr)                \
//     do {                                            \
//         const char *__mel_s = (cstr);               \
//         mel_sb_append_buf((sb), __mel_s, strlen(__mel_s)); \
//     } while (0)

// #define mel_sb_append_null(sb) mel_array_push((sb), '\0')

// #define mel_sb_free(sb) mel_array_free(sb)

// #define mel_sb_to_str8(sb) str8_from_parts((u8*)(sb)->items, (sb)->count)
