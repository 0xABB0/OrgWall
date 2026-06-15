#pragma once

#include <allocator/allocator.h>

#include <concepts>
#include <stddef.h>

namespace melody::allocator {

template<typename A>
concept allocator_c = requires(A a, size_t size) {
    { a.alloc(size) } -> std::same_as<void*>;
    { a.calloc(size) } -> std::same_as<void*>;
};

template<typename R>
concept reallocator_c = requires(R r, size_t size, void* p) {
    { r.realloc(p, size) } -> std::same_as<void*>;
};

template<typename D>
concept deallocator_c = requires(D d, void* p) {
    {d.dealloc(p)};
};

template<typename AA>
concept allocator_aligned_c = requires(AA a, void* p, size_t size, size_t alignment) {
    {a.alloc(size, alignment)} -> std::same_as<void*>;
    {a.calloc(size, alignment)} -> std::same_as<void*>;
};

template<typename RA>
concept reallocator_aligned_c = requires(RA r, void* p, size_t size, size_t alignment) {
    { r.realloc(p, size, alignment) } -> std::same_as<void*>;
};

template<typename DA>
concept deallocator_aligned_c = requires(DA d, void* p, size_t alignment) {
    {d.dealloc(p, alignment)};
};

template<allocator_c A, typename T>
static inline T* alloc(A a);

}

#include <allocator/allocator.ii>
