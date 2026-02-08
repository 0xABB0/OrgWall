#include "async.fiber.h"
#include "allocator.vmem.h"

Mel_Fiber_Transfer jump_fcontext(Mel_Fiber to, void* user);
Mel_Fiber          make_fcontext(void* sp, usize size, Mel_Fiber_Cb cb);

bool mel_fiber_stack_init(Mel_Fiber_Stack* fstack, u32 size)
{
    if (size == 0)
        size = MEL_FIBER_DEFAULT_STACK_SIZE;

    usize page_sz = mel_vmem_page_size();
    usize aligned = mel_vmem_align_to_page(size);

    void* base = mel_vmem_reserve(aligned);
    if (!base)
        return false;

    mel_vmem_protect(base, page_sz, MEL_VMEM_PROT_NONE);

    if (!mel_vmem_commit((u8*)base + page_sz, aligned - page_sz))
    {
        mel_vmem_release(base, aligned);
        return false;
    }

    fstack->sptr  = (u8*)base + aligned;
    fstack->ssize = (u32)aligned;
    return true;
}

void mel_fiber_stack_init_ptr(Mel_Fiber_Stack* fstack, void* ptr, u32 size)
{
    usize page_sz = mel_vmem_page_size();
    assert((uintptr_t)ptr % page_sz == 0);
    assert(size % page_sz == 0);
    MEL_UNUSED(page_sz);

    fstack->sptr  = ptr;
    fstack->ssize = size;
}

void mel_fiber_stack_release(Mel_Fiber_Stack* fstack)
{
    assert(fstack->sptr);
    void* base = (u8*)fstack->sptr - fstack->ssize;
    mel_vmem_release(base, fstack->ssize);
}

Mel_Fiber mel_fiber_create(Mel_Fiber_Stack stack, Mel_Fiber_Cb cb)
{
    return make_fcontext(stack.sptr, stack.ssize, cb);
}

Mel_Fiber_Transfer mel_fiber_switch(Mel_Fiber to, void* user)
{
    return jump_fcontext(to, user);
}
