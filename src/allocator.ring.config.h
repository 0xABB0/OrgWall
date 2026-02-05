#pragma once

#ifndef MEL_ALLOCATOR_RING_DEBUG

# if defined(MEL_DEBUG) | !defined(NDEBUG) | defined(_CLANGD)
#  define MEL_ALLOCATOR_RING_DEBUG 1
# else
# define MEL_ALLOCATOR_RING_DEBUG 0
# endif

#endif
