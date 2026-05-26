#include <stdio.h>

#include <core/types.h>
#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection.slotmap/slotmap.h>
#include <string/str8.h>

// Drives a few melody core facilities (heap allocator, slotmap, str8) so the
// run proves the library actually works under wasi, not just that it links.
int main(void)
{
    const Mel_Alloc* alloc = mel_alloc_heap();

    Mel_SlotMap nums;
    mel_slotmap_init(&nums, alloc, .item_size = sizeof(i32), .initial_capacity = 8);

    for (i32 i = 0; i < 16; i++) {
        i32 sq = i * i;
        mel_slotmap_insert(&nums, &sq);
    }

    i64 sum = 0;
    u32 count = mel_slotmap_count(&nums);
    const i32* data = (const i32*)mel_slotmap_data(&nums);
    for (u32 i = 0; i < count; i++) sum += data[i];

    str8 tag = str8_from_cstr("melody on wasi-sdk");
    printf("%.*s: %u slots, sum of squares 0..15 = %lld\n",
           (int)tag.len, (const char*)tag.data, count, (long long)sum);

    mel_slotmap_free(&nums);
    return 0;
}
