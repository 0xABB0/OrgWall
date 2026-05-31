#pragma once

#include <color/space.h>
#include <color/xyz.h>

mel_xyz mel_xyz_adapt_bradford(mel_xyz c, mel_white_point src, mel_white_point dst);
mel_xyz mel_xyz_adapt_cat02(mel_xyz c, mel_white_point src, mel_white_point dst);
mel_xyz mel_xyz_adapt_von_kries(mel_xyz c, mel_white_point src, mel_white_point dst);
