#pragma once

#include <string/str8.fwd.h>

#if defined(__clang__)
#define MEL_ENUM_TO_STRING(enum_type) \
    __attribute__((annotate("mel:enum_to_string"))) str8 enum_type##_to_string(enum_type)
#define MEL_ENUM_TO_STRING_DEFAULT(enum_type, default_label) \
    __attribute__((annotate("mel:enum_to_string:" default_label))) str8 enum_type##_to_string(enum_type)
#define MEL_STR(label) __attribute__((annotate("mel:str:" label)))
#define MEL_SKIP       __attribute__((annotate("mel:skip")))
#else
#define MEL_ENUM_TO_STRING(enum_type)                  str8 enum_type##_to_string(enum_type)
#define MEL_ENUM_TO_STRING_DEFAULT(enum_type, default_label) str8 enum_type##_to_string(enum_type)
#define MEL_STR(label)
#define MEL_SKIP
#endif
