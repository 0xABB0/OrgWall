#pragma once

#include "core.engine.h"

typedef int (*Mel_Cli_Main_Func)(int argc, char** argv);

typedef struct {
    Mel_Cli_Main_Func main;
} Mel_Cli_Opt;

#define MEL_CLI(...)                                                    \
    static Mel_Cli_Opt s_mel_cli_opt = (Mel_Cli_Opt){__VA_ARGS__};     \
    int main(int argc, char** argv) {                                   \
        mel__engine_init();                                             \
        int result = 0;                                                 \
        if (s_mel_cli_opt.main)                                         \
            result = s_mel_cli_opt.main(argc, argv);                    \
        mel__engine_shutdown();                                         \
        return result;                                                  \
    }
