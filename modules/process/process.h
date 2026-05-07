#pragma once

#include <stdint.h> // TODO: remove and replace with core types

typedef uint64_t Mel_Proc;
typedef uint64_t Mel_Fd;
typedef uint64_t Mel_Cmd;

Mel_Proc mel__cmd_start_process(Mel_Cmd cmd, Mel_Fd *fdin, Mel_Fd *fdout, Mel_Fd *fderr);
