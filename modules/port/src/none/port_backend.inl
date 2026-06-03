#ifdef _CLANGD
#pragma once
#include "../port.c"
#endif

bool mel_port__backend_available(void) { return false; }

bool mel_port__backend_port_init(Mel_Port* port)
{
    (void)port;
    return false;
}

void mel_port__backend_port_teardown(Mel_Port* port) { (void)port; }

void mel_port__backend_submit(Mel_Port_Op_Record* op) { mel_port__op_settle(op, 0, 0, MEL_PORT_ERROR | MEL_PORT_UNAVAILABLE); }

void mel_port__backend_retract(Mel_Port_Op_Record* op) { (void)op; }
