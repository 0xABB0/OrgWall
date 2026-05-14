#pragma once

#include <core/types.h>

#define MEL_VMEM_PROT_NONE  0
#define MEL_VMEM_PROT_READ  1
#define MEL_VMEM_PROT_WRITE 2
#define MEL_VMEM_PROT_EXEC  4

usize mel_vmem_page_size(void);

void* mel_vmem_reserve(usize size);
void  mel_vmem_release(void* ptr, usize size);
bool  mel_vmem_commit(void* ptr, usize size);
void  mel_vmem_decommit(void* ptr, usize size);
bool  mel_vmem_protect(void* ptr, usize size, i32 protection);

static inline usize mel_vmem_align_to_page(usize size)
{
    usize page = mel_vmem_page_size();
    return (size + page - 1) & ~(page - 1);
}
