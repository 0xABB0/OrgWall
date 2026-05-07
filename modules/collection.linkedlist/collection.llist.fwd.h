#pragma once

#define Mel_LListNode(T) struct { T value; void* next; void* prev; }

#define Mel_LList(T) struct { \
    struct { T value; void* next; void* prev; } *head, *tail; \
    usize count; \
    const Mel_Alloc* allocator; \
}
