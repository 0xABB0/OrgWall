#pragma once

#include <core/types.h>

typedef struct Mel_ListNode Mel_ListNode;

struct Mel_ListNode
{
    Mel_ListNode* next;
    Mel_ListNode* prev;
};

#define mel_container_of(ptr, type, member) \
    ((type*)((u8*)(ptr) - offsetof(type, member)))

#define mel_list_entry(ptr, type, member) mel_container_of(ptr, type, member)

#define mel_list_foreach(pos, head) \
    for (Mel_ListNode* pos = (head)->next; pos != (head); pos = pos->next)

#define mel_list_foreach_safe(pos, tmp, head) \
    for (Mel_ListNode* pos = (head)->next, *tmp = pos->next; pos != (head); pos = tmp, tmp = pos->next)

static inline void mel_list_init(Mel_ListNode* node);
static inline void mel_list_push_front(Mel_ListNode* head, Mel_ListNode* node);
static inline void mel_list_push_back(Mel_ListNode* head, Mel_ListNode* node);
static inline void mel_list_remove(Mel_ListNode* node);
static inline bool mel_list_empty(Mel_ListNode* head);
static inline Mel_ListNode* mel_list_front(Mel_ListNode* head);
static inline Mel_ListNode* mel_list_back(Mel_ListNode* head);
static inline void mel_list_splice(Mel_ListNode* dst, Mel_ListNode* src);
static inline usize mel_list_count(Mel_ListNode* head);

#include "collection.list.inl"
