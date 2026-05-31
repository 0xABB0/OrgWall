#pragma once

#include <color/lab.h>
#include <color/oklab.h>

float mel_delta_e_76(mel_lab a, mel_lab b);
float mel_delta_e_94(mel_lab a, mel_lab b);
float mel_delta_e_2000(mel_lab a, mel_lab b);
float mel_delta_e_ok(mel_oklab a, mel_oklab b);
