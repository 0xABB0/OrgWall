#pragma once

#include <port/port.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Port_Op_Record Mel_Port_Op_Record;

bool mel_port__backend_available(void);

bool mel_port__backend_port_init(Mel_Port* port);
void mel_port__backend_port_teardown(Mel_Port* port);

void mel_port__backend_submit(Mel_Port_Op_Record* op);
void mel_port__backend_retract(Mel_Port_Op_Record* op);

#ifdef __cplusplus
}
#endif
