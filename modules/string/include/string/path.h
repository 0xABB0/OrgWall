#pragma once

#include "str8.fwd.h"

str8  mel_path_normalize(str8 path, u8* buf, usize buf_cap);
str8  mel_path_join(str8 base, str8 relative, u8* buf, usize buf_cap);
str8  mel_path_parent(str8 path);
str8  mel_path_filename(str8 path);
str8  mel_path_extension(str8 path);
bool  mel_path_is_absolute(str8 path);
