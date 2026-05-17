#pragma once

#include "debug.cfg.h"

#ifndef MEL_STACKTRACE_ENABLED

# if MEL_DEBUG
#  define MEL_STACKTRACE_ENABLED 1
# else
#  define MEL_STACKTRACE_ENABLED 0
# endif

#endif



#ifndef MEL_STACKTRACE_HAS_FUNCTION_NAMES

# if MEL_STACKTRACE_ENABLED
#  define MEL_STACKTRACE_HAS_FUNCTION_NAMES 1
# else
#  define MEL_STACKTRACE_HAS_FUNCTION_NAMES 0
# endif

#endif



#ifndef MEL_STACKTRACE_HAS_SOURCE_INFO

# if MEL_DEBUG
#  define MEL_STACKTRACE_HAS_SOURCE_INFO 1
# else
#  define MEL_STACKTRACE_HAS_SOURCE_INFO 0
# endif

#endif
