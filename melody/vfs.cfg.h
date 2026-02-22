#pragma once

#ifndef MEL_VFS_MAX_PATH
# define MEL_VFS_MAX_PATH 4096
#endif

#ifndef MEL_VFS_MOUNT_TABLE_INITIAL
# define MEL_VFS_MOUNT_TABLE_INITIAL 16
#endif

#ifndef MEL_VFS_RING_BUFFER_SIZE
# define MEL_VFS_RING_BUFFER_SIZE (64u * 1024u)
#endif

#ifndef MEL_VFS_VALIDATE_HANDLES
# if defined(MEL_DEBUG) || !defined(NDEBUG) || defined(_CLANGD)
#  define MEL_VFS_VALIDATE_HANDLES 1
# else
#  define MEL_VFS_VALIDATE_HANDLES 0
# endif
#endif
