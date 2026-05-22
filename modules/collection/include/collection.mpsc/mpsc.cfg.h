#pragma once

#ifndef MEL_COLLECTION_MPSC_DEBUG

# if defined(MEL_DEBUG) | !defined(NDEBUG) | defined(_CLANGD)
#  define MEL_COLLECTION_MPSC_DEBUG 1
# else
#  define MEL_COLLECTION_MPSC_DEBUG 0
# endif

#endif
