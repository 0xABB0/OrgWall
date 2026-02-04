#pragma once

#ifndef MEL_ALLOCATOR_ARENA_DEBUG

# if defined(MEL_DEBUG) | defined(NDEBUG) | defined(_CLANGD)
#  define MEL_ALLOCATOR_ARENA_DEBUG 1
# else
# define MEL_ALLOCATOR_ARENA_DEBUG 0
# endif

#endif
