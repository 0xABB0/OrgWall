#pragma once

#include "debug.h"


#ifndef MEL_ASSERT_ENABLED

# if MEL_DEBUG
#  define MEL_ASSERT_ENABLED 1
# else
#  define MEL_ASSERT_ENABLED 0
# endif

#endif
