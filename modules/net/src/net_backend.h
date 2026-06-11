#pragma once

#include <net/net.h>
#include <net/address.h>

#include <core/types.h>
#include <allocator/allocator.fwd.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define MEL_NET__WOULD_BLOCK (-1)

typedef struct
{
    i32  fd;
    bool pending;
    i32  os_error;
} Mel_Net__Connect_R;

bool mel_net__backend_available(void);
u32  mel_net__backend_scope_id(const char* name);

Mel_Net_Status mel_net__backend_status_from_os(i32 os_error);

Mel_Net__Connect_R mel_net__backend_connect_begin(const Mel_Net_Address* addr, bool nodelay);
i32                mel_net__backend_connect_finish(i32 fd);

i32 mel_net__backend_listen(const Mel_Net_Address* addr, u32 backlog, bool reuse_addr, bool v6_only, i32* out_fd);
i32 mel_net__backend_accept(i32 listener_fd, i32* out_fd, Mel_Net_Address* out_peer);

i32 mel_net__backend_udp_open(const Mel_Net_Address* bind_addr, bool reuse_addr, i32* out_fd);
i32 mel_net__backend_sendto(i32 fd, const void* buffer, usize len, const Mel_Net_Address* to, usize* out_sent);
i32 mel_net__backend_recvfrom(i32 fd, void* buffer, usize len, Mel_Net_Address* out_from, bool* out_truncated, usize* out_received);

i32  mel_net__backend_local_address(i32 fd, Mel_Net_Address* out);
i32  mel_net__backend_peer_address(i32 fd, Mel_Net_Address* out);
void mel_net__backend_shutdown(i32 fd, bool read, bool write);
void mel_net__backend_close(i32 fd);

Mel_Net_Status mel_net__backend_resolve(const char* host, u16 port, bool v4_only, bool v6_only, const Mel_Alloc* alloc, Mel_Net_Address** out_items, usize* out_count, i32* out_os_error);

#ifdef __cplusplus
}
#endif
