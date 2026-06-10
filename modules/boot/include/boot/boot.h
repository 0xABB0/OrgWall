#pragma once

typedef struct Mel_Vat Mel_Vat;

void mel_app_setup(Mel_Vat* root);

int    mel_app_argc(void);
char** mel_app_argv(void);
void   mel_app_set_exit_code(int code);
void   mel_app_on_exit(void (*fn)(void* user), void* user);
