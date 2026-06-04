#pragma once

#ifdef _CLANGD
#include "list.h"
#endif

static inline void mel_list_init(Mel_ListNode* node)
{
    node->next = node;
    node->prev = node;
}

static inline void mel__list_insert(Mel_ListNode* prev, Mel_ListNode* next, Mel_ListNode* node)
{
    node->next = next;
    node->prev = prev;
    prev->next = node;
    next->prev = node;
}

static inline void mel_list_push_front(Mel_ListNode* head, Mel_ListNode* node)
{
    mel__list_insert(head, head->next, node);
}

static inline void mel_list_push_back(Mel_ListNode* head, Mel_ListNode* node)
{
    mel__list_insert(head->prev, head, node);
}

static inline void mel_list_remove(Mel_ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

static inline bool mel_list_empty(Mel_ListNode* head)
{
    return head->next == head;
}

static inline Mel_ListNode* mel_list_front(Mel_ListNode* head)
{
    return head->next;
}

static inline Mel_ListNode* mel_list_back(Mel_ListNode* head)
{
    return head->prev;
}

static inline void mel_list_splice(Mel_ListNode* dst, Mel_ListNode* src)
{
    if (mel_list_empty(src)) return;

    Mel_ListNode* src_first = src->next;
    Mel_ListNode* src_last = src->prev;

    Mel_ListNode* dst_first = dst->next;

    dst->next = src_first;
    src_first->prev = dst;

    src_last->next = dst_first;
    dst_first->prev = src_last;

    mel_list_init(src);
}

static inline usize mel_list_count(Mel_ListNode* head)
{
    usize n = 0;
    mel_list_foreach(pos, head)
    {
        n++;
    }
    return n;
}
