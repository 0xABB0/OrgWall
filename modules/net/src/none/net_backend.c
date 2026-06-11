#include "../net_backend.h"

bool mel_net__backend_available(void) { return false; }

u32 mel_net__backend_scope_id(const char* name)
{
    (void)name;
    return 0;
}

Mel_Net_Status mel_net__backend_status_from_os(i32 e)
{
    (void)e;
    return MEL_NET_ERROR | MEL_NET_UNAVAILABLE;
}

Mel_Net__Connect_R mel_net__backend_connect_begin(const Mel_Net_Address* addr, bool nodelay)
{
    (void)addr;
    (void)nodelay;
    return (Mel_Net__Connect_R){ .fd = -1 };
}

i32 mel_net__backend_connect_finish(i32 fd)
{
    (void)fd;
    return -1;
}

i32 mel_net__backend_listen(const Mel_Net_Address* addr, u32 backlog, bool reuse_addr, bool v6_only, i32* out_fd)
{
    (void)addr;
    (void)backlog;
    (void)reuse_addr;
    (void)v6_only;
    (void)out_fd;
    return -1;
}

i32 mel_net__backend_accept(i32 listener_fd, i32* out_fd, Mel_Net_Address* out_peer)
{
    (void)listener_fd;
    (void)out_fd;
    (void)out_peer;
    return -1;
}

i32 mel_net__backend_udp_open(const Mel_Net_Address* bind_addr, bool reuse_addr, i32* out_fd)
{
    (void)bind_addr;
    (void)reuse_addr;
    (void)out_fd;
    return -1;
}

i32 mel_net__backend_sendto(i32 fd, const void* buffer, usize len, const Mel_Net_Address* to, usize* out_sent)
{
    (void)fd;
    (void)buffer;
    (void)len;
    (void)to;
    (void)out_sent;
    return -1;
}

i32 mel_net__backend_recvfrom(i32 fd, void* buffer, usize len, Mel_Net_Address* out_from, bool* out_truncated, usize* out_received)
{
    (void)fd;
    (void)buffer;
    (void)len;
    (void)out_from;
    (void)out_truncated;
    (void)out_received;
    return -1;
}

i32 mel_net__backend_local_address(i32 fd, Mel_Net_Address* out)
{
    (void)fd;
    (void)out;
    return -1;
}

i32 mel_net__backend_peer_address(i32 fd, Mel_Net_Address* out)
{
    (void)fd;
    (void)out;
    return -1;
}

void mel_net__backend_shutdown(i32 fd, bool read, bool write)
{
    (void)fd;
    (void)read;
    (void)write;
}

void mel_net__backend_close(i32 fd) { (void)fd; }

Mel_Net_Status mel_net__backend_resolve(const char* host, u16 port, bool v4_only, bool v6_only, const Mel_Alloc* alloc, Mel_Net_Address** out_items, usize* out_count, i32* out_os_error)
{
    (void)host;
    (void)port;
    (void)v4_only;
    (void)v6_only;
    (void)alloc;
    (void)out_items;
    (void)out_count;
    *out_os_error = 0;
    return MEL_NET_ERROR | MEL_NET_UNAVAILABLE;
}
