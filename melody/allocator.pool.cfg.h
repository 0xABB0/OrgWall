#pragma once

#ifndef MEL_ALLOCATOR_POOL_DEBUG

# if defined(MEL_DEBUG) | !defined(NDEBUG) | defined(_CLANGD)
#  define MEL_ALLOCATOR_POOL_DEBUG 1
# else
# define MEL_ALLOCATOR_POOL_DEBUG 0
# endif

#endif
