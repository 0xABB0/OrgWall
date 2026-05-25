#include <allocator/leak.h>
#include <allocator/allocator.h>
#include <stdlib.h>

extern Mel_Mem_Fail_Cb mel__get_fail_cb(void);

typedef struct Mel_Leak_Header Mel_Leak_Header;

struct Mel_Leak_Header
{
    Mel_Leak_Header* prev;
    Mel_Leak_Header* next;
    const char* file;
    const char* func;
    u32 line;
    usize size;
};

#define MEL_LEAK_FREED_MAGIC (~(usize)0)

static Mel_Leak_Header s_leak_sentinel = {
    .prev = &s_leak_sentinel,
    .next = &s_leak_sentinel,
    .file = NULL,
    .func = NULL,
    .line = 0,
    .size = 0
};

static void* leak_detect_cb(void* ptr, usize size, u32 align,
                            const char* file, const char* func, u32 line,
                            void* user_data)
{
    MEL_UNUSED(align);
    MEL_UNUSED(user_data);

    Mel_Mem_Fail_Cb fail_cb = mel__get_fail_cb();

    if (ptr == NULL && size > 0)
    {
        Mel_Leak_Header* header = (Mel_Leak_Header*)malloc(sizeof(Mel_Leak_Header) + size);
        if (!header)
        {
            if (fail_cb) fail_cb(file, func, line, size);
            return NULL;
        }
        header->file = file;
        header->func = func;
        header->line = line;
        header->size = size;

        header->next = s_leak_sentinel.next;
        header->prev = &s_leak_sentinel;
        s_leak_sentinel.next->prev = header;
        s_leak_sentinel.next = header;

        return (void*)(header + 1);
    }

    if (ptr != NULL && size > 0)
    {
        Mel_Leak_Header* header = ((Mel_Leak_Header*)ptr) - 1;
        assert(header->size != MEL_LEAK_FREED_MAGIC);

        header->prev->next = header->next;
        header->next->prev = header->prev;

        Mel_Leak_Header* new_header = (Mel_Leak_Header*)realloc(header, sizeof(Mel_Leak_Header) + size);
        if (!new_header)
        {
            header->next = s_leak_sentinel.next;
            header->prev = &s_leak_sentinel;
            s_leak_sentinel.next->prev = header;
            s_leak_sentinel.next = header;
            if (fail_cb) fail_cb(file, func, line, size);
            return NULL;
        }
        new_header->file = file;
        new_header->func = func;
        new_header->line = line;
        new_header->size = size;

        new_header->next = s_leak_sentinel.next;
        new_header->prev = &s_leak_sentinel;
        s_leak_sentinel.next->prev = new_header;
        s_leak_sentinel.next = new_header;

        return (void*)(new_header + 1);
    }

    if (ptr != NULL && size == 0)
    {
        Mel_Leak_Header* header = ((Mel_Leak_Header*)ptr) - 1;
        assert(header->size != MEL_LEAK_FREED_MAGIC);

        header->prev->next = header->next;
        header->next->prev = header->prev;
        header->size = MEL_LEAK_FREED_MAGIC;
        free(header);
        return NULL;
    }

    return NULL;
}

static const Mel_Alloc s_leak_detect_alloc = {
    .alloc_cb = leak_detect_cb,
    .user_data = NULL
};

const Mel_Alloc* mel_alloc_leak_detect(void)
{
    return &s_leak_detect_alloc;
}

void mel_leak_dump(Mel_Leak_Report_Cb cb, void* user_data)
{
    Mel_Leak_Header* h = s_leak_sentinel.next;
    while (h != &s_leak_sentinel)
    {
        cb(h->file, h->func, h->line, h->size, user_data);
        h = h->next;
    }
}
