#pragma once

#include <string/string.str8.h>

// TODO: most of these information is not available if not in debug.
//    maybe a configuration like
//    MEL_STACKFRAME_INCLUDE_FILENAME
//    and the likes could help make this better :)

typedef struct Mel_Stackframe Mel_Stackframe;

struct Mel_Stackframe
{
    str8  filename;
    usize file_line;
    str8  function_name;
    usize column;
    usize offset;
};
