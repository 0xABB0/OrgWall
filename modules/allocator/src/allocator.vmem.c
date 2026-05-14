#include <allocator.vmem/vmem.h>
#include <core/platform.h>

#if MEL_PLATFORM_WINDOWS
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#elif MEL_PLATFORM_POSIX
    #include <sys/mman.h>
    #include <unistd.h>
    #if defined(MAP_ANON) && !defined(MAP_ANONYMOUS)
        #define MAP_ANONYMOUS MAP_ANON
    #endif
#endif

usize mel_vmem_page_size(void)
{
#if MEL_PLATFORM_WINDOWS
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (usize)si.dwPageSize;
#elif MEL_PLATFORM_POSIX
    return (usize)sysconf(_SC_PAGESIZE);
#else
    return 4096;
#endif
}

void* mel_vmem_reserve(usize size)
{
#if MEL_PLATFORM_WINDOWS
    return VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_NOACCESS);
#elif MEL_PLATFORM_POSIX
    void* ptr = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return (ptr == MAP_FAILED) ? NULL : ptr;
#else
    return NULL;
#endif
}

void mel_vmem_release(void* ptr, usize size)
{
#if MEL_PLATFORM_WINDOWS
    (void)size;
    VirtualFree(ptr, 0, MEM_RELEASE);
#elif MEL_PLATFORM_POSIX
    munmap(ptr, size);
#endif
}

bool mel_vmem_commit(void* ptr, usize size)
{
#if MEL_PLATFORM_WINDOWS
    return VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE) != NULL;
#elif MEL_PLATFORM_POSIX
    return mprotect(ptr, size, PROT_READ | PROT_WRITE) == 0;
#else
    return false;
#endif
}

void mel_vmem_decommit(void* ptr, usize size)
{
#if MEL_PLATFORM_WINDOWS
    VirtualFree(ptr, size, MEM_DECOMMIT);
#elif MEL_PLATFORM_POSIX
    madvise(ptr, size, MADV_DONTNEED);
    mprotect(ptr, size, PROT_NONE);
#endif
}

bool mel_vmem_protect(void* ptr, usize size, i32 protection)
{
#if MEL_PLATFORM_WINDOWS
    DWORD prot = PAGE_NOACCESS;
    if (protection & MEL_VMEM_PROT_EXEC)
    {
        if (protection & MEL_VMEM_PROT_WRITE)      prot = PAGE_EXECUTE_READWRITE;
        else if (protection & MEL_VMEM_PROT_READ)   prot = PAGE_EXECUTE_READ;
        else                                         prot = PAGE_EXECUTE;
    }
    else
    {
        if (protection & MEL_VMEM_PROT_WRITE)        prot = PAGE_READWRITE;
        else if (protection & MEL_VMEM_PROT_READ)   prot = PAGE_READONLY;
    }
    DWORD old;
    return VirtualProtect(ptr, size, prot, &old) != 0;
#elif MEL_PLATFORM_POSIX
    int prot = PROT_NONE;
    if (protection & MEL_VMEM_PROT_READ)  prot |= PROT_READ;
    if (protection & MEL_VMEM_PROT_WRITE) prot |= PROT_WRITE;
    if (protection & MEL_VMEM_PROT_EXEC)  prot |= PROT_EXEC;
    return mprotect(ptr, size, prot) == 0;
#else
    return false;
#endif
}
