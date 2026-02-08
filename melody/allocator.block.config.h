#pragma once

#ifndef MEL_ALLOCATOR_BLOCK_DEBUG

# if defined(MEL_DEBUG) | !defined(NDEBUG) | defined(_CLANGD)
#  define MEL_ALLOCATOR_BLOCK_DEBUG 1
# else
# define MEL_ALLOCATOR_BLOCK_DEBUG 0
# endif

#endif
