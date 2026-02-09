#pragma once

#include "allocator.h"
#include "collection.llist.fwd.h"

#define mel_llist_init(ll, alloc) do { \
    (ll)->head = nullptr; \
    (ll)->tail = nullptr; \
    (ll)->count = 0; \
    (ll)->allocator = (alloc); \
} while (0)

#define mel_llist_free(ll) do { \
    mel_llist_clear(ll); \
} while (0)

#define mel_llist_push_front(ll, item) do { \
    typeof((ll)->head) mel__node = mel_alloc((ll)->allocator, sizeof(*(ll)->head)); \
    mel__node->value = (item); \
    mel__node->next = (ll)->head; \
    mel__node->prev = nullptr; \
    if ((ll)->head != nullptr) { \
        ((typeof(mel__node))(ll)->head)->prev = mel__node; \
    } else { \
        (ll)->tail = mel__node; \
    } \
    (ll)->head = mel__node; \
    (ll)->count++; \
} while (0)

#define mel_llist_push_back(ll, item) do { \
    typeof((ll)->head) mel__node = mel_alloc((ll)->allocator, sizeof(*(ll)->head)); \
    mel__node->value = (item); \
    mel__node->next = nullptr; \
    mel__node->prev = (ll)->tail; \
    if ((ll)->tail != nullptr) { \
        ((typeof(mel__node))(ll)->tail)->next = mel__node; \
    } else { \
        (ll)->head = mel__node; \
    } \
    (ll)->tail = mel__node; \
    (ll)->count++; \
} while (0)

#define mel_llist_pop_front(ll) __extension__({ \
    typeof((ll)->head) mel__node = (ll)->head; \
    typeof(mel__node->value) mel__val = mel__node->value; \
    (ll)->head = mel__node->next; \
    if ((ll)->head != nullptr) { \
        ((typeof(mel__node))(ll)->head)->prev = nullptr; \
    } else { \
        (ll)->tail = nullptr; \
    } \
    mel_dealloc((ll)->allocator, mel__node); \
    (ll)->count--; \
    mel__val; \
})

#define mel_llist_pop_back(ll) __extension__({ \
    typeof((ll)->tail) mel__node = (ll)->tail; \
    typeof(mel__node->value) mel__val = mel__node->value; \
    (ll)->tail = mel__node->prev; \
    if ((ll)->tail != nullptr) { \
        ((typeof(mel__node))(ll)->tail)->next = nullptr; \
    } else { \
        (ll)->head = nullptr; \
    } \
    mel_dealloc((ll)->allocator, mel__node); \
    (ll)->count--; \
    mel__val; \
})

#define mel_llist_front(ll) ((ll)->head->value)

#define mel_llist_back(ll) ((ll)->tail->value)

#define mel_llist_count(ll) ((ll)->count)

#define mel_llist_empty(ll) ((ll)->count == 0)

#define mel_llist_clear(ll) do { \
    typeof((ll)->head) mel__cur = (ll)->head; \
    while (mel__cur != nullptr) { \
        typeof(mel__cur) mel__next = mel__cur->next; \
        mel_dealloc((ll)->allocator, mel__cur); \
        mel__cur = mel__next; \
    } \
    (ll)->head = nullptr; \
    (ll)->tail = nullptr; \
    (ll)->count = 0; \
} while (0)

#define mel_llist_foreach(ll, varname, body) do { \
    typeof((ll)->head) varname = (ll)->head; \
    while (varname != nullptr) { \
        typeof(varname) mel__next_##varname = varname->next; \
        { body; } \
        varname = mel__next_##varname; \
    } \
} while (0)

#define mel_llist_remove_node(ll, node_ptr) do { \
    typeof((ll)->head) mel__rn = (node_ptr); \
    if (mel__rn->prev != nullptr) { \
        ((typeof(mel__rn))mel__rn->prev)->next = mel__rn->next; \
    } else { \
        (ll)->head = mel__rn->next; \
    } \
    if (mel__rn->next != nullptr) { \
        ((typeof(mel__rn))mel__rn->next)->prev = mel__rn->prev; \
    } else { \
        (ll)->tail = mel__rn->prev; \
    } \
    mel_dealloc((ll)->allocator, mel__rn); \
    (ll)->count--; \
} while (0)
