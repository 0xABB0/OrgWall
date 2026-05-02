#pragma once

#include "core.types.h"
#include "collection.slotmap.fwd.h"
#include "string.str8.fwd.h"

typedef struct Mel_Server                Mel_Server;
typedef struct Mel_Server_Req            Mel_Server_Req;
typedef struct Mel_Server_Conn           Mel_Server_Conn;
typedef struct Mel_Server_RPC_Req        Mel_Server_RPC_Req;

typedef Mel_SlotMap_Handle               Mel_Server_Handle;
typedef Mel_SlotMap_Handle               Mel_Server_Route_Handle;
typedef Mel_SlotMap_Handle               Mel_Server_Conn_Handle;
typedef Mel_SlotMap_Handle               Mel_Server_RPC_Method_Handle;
typedef Mel_SlotMap_Handle               Mel_Server_Listener_Handle;

#define MEL_SERVER_HANDLE_NULL           MEL_SLOTMAP_HANDLE_NULL
#define mel_server_handle_valid          mel_slotmap_handle_valid

#define MEL_SERVER_OK                    0
#define MEL_SERVER_ERR_NOT_INITIALIZED   1
#define MEL_SERVER_ERR_ALREADY_INITIALIZED 2
#define MEL_SERVER_ERR_INVALID_HANDLE    3
#define MEL_SERVER_ERR_INVALID_ARGUMENT  4
#define MEL_SERVER_ERR_OOM               5
#define MEL_SERVER_ERR_BIND              6
#define MEL_SERVER_ERR_LISTEN            7
#define MEL_SERVER_ERR_TLS               8
#define MEL_SERVER_ERR_TLS_DISABLED      9
#define MEL_SERVER_ERR_NOT_FOUND         10
#define MEL_SERVER_ERR_INVALID_PATH      11
#define MEL_SERVER_ERR_QUEUE_FULL        12
#define MEL_SERVER_ERR_CONN_CLOSED       13
#define MEL_SERVER_ERR_UNIMPLEMENTED     14

typedef i32 Mel_Server_MW_Result;
#define MEL_SERVER_MW_NEXT 0
#define MEL_SERVER_MW_STOP 1

typedef void                  (*Mel_Server_Route_Fn)   (Mel_Server_Req* req);
typedef Mel_Server_MW_Result  (*Mel_Server_MW_Fn)      (Mel_Server_Req* req);
typedef void                  (*Mel_Server_WS_Open_Fn) (Mel_Server_Handle s, Mel_Server_Conn_Handle c);
typedef void                  (*Mel_Server_WS_Msg_Fn)  (Mel_Server_Handle s, Mel_Server_Conn_Handle c, str8 data, bool is_binary);
typedef void                  (*Mel_Server_WS_Close_Fn)(Mel_Server_Handle s, Mel_Server_Conn_Handle c);
typedef void                  (*Mel_Server_RPC_Fn)     (Mel_Server_RPC_Req* r);
typedef void                  (*Mel_Server_Main_Fn)    (Mel_Server_Handle s, void* user_data);
