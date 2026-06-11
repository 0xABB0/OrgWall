#include "../net_backend.h"

#include <allocator/allocator.h>

#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

bool mel_net__backend_available(void) { return true; }

u32 mel_net__backend_scope_id(const char* name) { return if_nametoindex(name); }

Mel_Net_Status mel_net__backend_status_from_os(i32 e)
{
    switch (e)
    {
    case 0:
        return MEL_NET_OK;
    case ECONNREFUSED:
        return MEL_NET_ERROR | MEL_NET_REFUSED;
    case ETIMEDOUT:
        return MEL_NET_ERROR | MEL_NET_TIMED_OUT;
    case EHOSTUNREACH:
    case ENETUNREACH:
    case ENETDOWN:
        return MEL_NET_ERROR | MEL_NET_UNREACHABLE;
    case ECONNRESET:
    case EPIPE:
        return MEL_NET_ERROR | MEL_NET_RESET;
    case EADDRINUSE:
    case EADDRNOTAVAIL:
        return MEL_NET_ERROR | MEL_NET_IN_USE;
    default:
        return MEL_NET_ERROR;
    }
}

static void net_posix_prepare_fd(i32 fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1)
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
}

static usize addr_to_ss(const Mel_Net_Address* a, struct sockaddr_storage* ss)
{
    memset(ss, 0, sizeof *ss);
    if (!a->v6)
    {
        struct sockaddr_in* sin = (struct sockaddr_in*)ss;
        sin->sin_family = AF_INET;
        sin->sin_port = htons(a->port);
        memcpy(&sin->sin_addr, a->bytes, 4);
        return sizeof *sin;
    }
    struct sockaddr_in6* sin6 = (struct sockaddr_in6*)ss;
    sin6->sin6_family = AF_INET6;
    sin6->sin6_port = htons(a->port);
    memcpy(&sin6->sin6_addr, a->bytes, 16);
    sin6->sin6_scope_id = a->scope_id;
    return sizeof *sin6;
}

static void addr_from_ss(Mel_Net_Address* a, const struct sockaddr_storage* ss)
{
    memset(a, 0, sizeof *a);
    if (ss->ss_family == AF_INET)
    {
        const struct sockaddr_in* sin = (const struct sockaddr_in*)ss;
        a->port = ntohs(sin->sin_port);
        memcpy(a->bytes, &sin->sin_addr, 4);
        return;
    }
    if (ss->ss_family == AF_INET6)
    {
        const struct sockaddr_in6* sin6 = (const struct sockaddr_in6*)ss;
        a->v6 = true;
        a->port = ntohs(sin6->sin6_port);
        memcpy(a->bytes, &sin6->sin6_addr, 16);
        a->scope_id = sin6->sin6_scope_id;
    }
}

Mel_Net__Connect_R mel_net__backend_connect_begin(const Mel_Net_Address* addr, bool nodelay)
{
    Mel_Net__Connect_R r = { .fd = -1 };

    i32 fd = socket(addr->v6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        r.os_error = errno;
        return r;
    }
    net_posix_prepare_fd(fd);
    if (nodelay)
    {
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    }

    struct sockaddr_storage ss;
    usize                   slen = addr_to_ss(addr, &ss);

    for (;;)
    {
        if (connect(fd, (struct sockaddr*)&ss, (socklen_t)slen) == 0)
        {
            r.fd = fd;
            return r;
        }
        if (errno == EINTR)
            continue;
        if (errno == EINPROGRESS)
        {
            r.fd = fd;
            r.pending = true;
            return r;
        }
        r.os_error = errno;
        close(fd);
        return r;
    }
}

i32 mel_net__backend_connect_finish(i32 fd)
{
    struct sockaddr_storage ss;
    socklen_t               slen = sizeof ss;
    if (getpeername(fd, (struct sockaddr*)&ss, &slen) == 0)
        return 0;
    if (errno == ENOTCONN || errno == EINVAL)
    {
        i32       probe = errno;
        int       err = 0;
        socklen_t len = sizeof err;
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0)
            return errno;
        if (err == 0)
            return probe == ENOTCONN ? MEL_NET__WOULD_BLOCK : probe;
        return err;
    }
    return errno;
}

i32 mel_net__backend_listen(const Mel_Net_Address* addr, u32 backlog, bool reuse_addr, bool v6_only, i32* out_fd)
{
    i32 fd = socket(addr->v6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return errno;
    net_posix_prepare_fd(fd);

    if (reuse_addr)
    {
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    }
    if (addr->v6)
    {
        int v = v6_only ? 1 : 0;
        setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &v, sizeof v);
    }

    struct sockaddr_storage ss;
    usize                   slen = addr_to_ss(addr, &ss);
    if (bind(fd, (struct sockaddr*)&ss, (socklen_t)slen) != 0)
    {
        i32 e = errno;
        close(fd);
        return e;
    }
    if (listen(fd, (int)backlog) != 0)
    {
        i32 e = errno;
        close(fd);
        return e;
    }
    *out_fd = fd;
    return 0;
}

i32 mel_net__backend_accept(i32 listener_fd, i32* out_fd, Mel_Net_Address* out_peer)
{
    struct sockaddr_storage ss;
    socklen_t               slen = sizeof ss;
    for (;;)
    {
        i32 fd = accept(listener_fd, (struct sockaddr*)&ss, &slen);
        if (fd >= 0)
        {
            net_posix_prepare_fd(fd);
            if (out_peer)
                addr_from_ss(out_peer, &ss);
            *out_fd = fd;
            return 0;
        }
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return MEL_NET__WOULD_BLOCK;
        return errno;
    }
}

i32 mel_net__backend_udp_open(const Mel_Net_Address* bind_addr, bool reuse_addr, i32* out_fd)
{
    bool v6 = bind_addr ? bind_addr->v6 : false;
    i32  fd = socket(v6 ? AF_INET6 : AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return errno;
    net_posix_prepare_fd(fd);

    if (reuse_addr)
    {
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    }
    if (bind_addr)
    {
        struct sockaddr_storage ss;
        usize                   slen = addr_to_ss(bind_addr, &ss);
        if (bind(fd, (struct sockaddr*)&ss, (socklen_t)slen) != 0)
        {
            i32 e = errno;
            close(fd);
            return e;
        }
    }
    *out_fd = fd;
    return 0;
}

i32 mel_net__backend_sendto(i32 fd, const void* buffer, usize len, const Mel_Net_Address* to, usize* out_sent)
{
    struct sockaddr_storage ss;
    usize                   slen = addr_to_ss(to, &ss);
    for (;;)
    {
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags |= MSG_NOSIGNAL;
#endif
        ssize_t n = sendto(fd, buffer, len, flags, (struct sockaddr*)&ss, (socklen_t)slen);
        if (n >= 0)
        {
            *out_sent = (usize)n;
            return 0;
        }
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return MEL_NET__WOULD_BLOCK;
        return errno;
    }
}

i32 mel_net__backend_recvfrom(i32 fd, void* buffer, usize len, Mel_Net_Address* out_from, bool* out_truncated, usize* out_received)
{
    struct sockaddr_storage ss;
    for (;;)
    {
        struct iovec  iov = { .iov_base = buffer, .iov_len = len };
        struct msghdr msg = { 0 };
        msg.msg_name = &ss;
        msg.msg_namelen = sizeof ss;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;

        ssize_t n = recvmsg(fd, &msg, 0);
        if (n >= 0)
        {
            if (out_from)
                addr_from_ss(out_from, &ss);
            *out_truncated = (msg.msg_flags & MSG_TRUNC) != 0;
            *out_received = (usize)n;
            return 0;
        }
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return MEL_NET__WOULD_BLOCK;
        return errno;
    }
}

i32 mel_net__backend_local_address(i32 fd, Mel_Net_Address* out)
{
    struct sockaddr_storage ss;
    socklen_t               slen = sizeof ss;
    if (getsockname(fd, (struct sockaddr*)&ss, &slen) != 0)
        return errno;
    addr_from_ss(out, &ss);
    return 0;
}

i32 mel_net__backend_peer_address(i32 fd, Mel_Net_Address* out)
{
    struct sockaddr_storage ss;
    socklen_t               slen = sizeof ss;
    if (getpeername(fd, (struct sockaddr*)&ss, &slen) != 0)
        return errno;
    addr_from_ss(out, &ss);
    return 0;
}

void mel_net__backend_shutdown(i32 fd, bool read, bool write)
{
    int how;
    if (read && write)
        how = SHUT_RDWR;
    else if (read)
        how = SHUT_RD;
    else if (write)
        how = SHUT_WR;
    else
        return;
    shutdown(fd, how);
}

void mel_net__backend_close(i32 fd)
{
    if (fd >= 0)
        close(fd);
}

Mel_Net_Status mel_net__backend_resolve(const char* host, u16 port, bool v4_only, bool v6_only, const Mel_Alloc* alloc, Mel_Net_Address** out_items, usize* out_count, i32* out_os_error)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = v4_only ? AF_INET : v6_only ? AF_INET6 : AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_ADDRCONFIG;

    struct addrinfo* list = NULL;
    int              rc = getaddrinfo(host, NULL, &hints, &list);
    if (rc != 0)
    {
        *out_os_error = rc;
        return MEL_NET_ERROR | MEL_NET_RESOLVE_FAILED;
    }

    usize count = 0;
    for (struct addrinfo* ai = list; ai; ai = ai->ai_next)
        if (ai->ai_family == AF_INET || ai->ai_family == AF_INET6)
            count++;

    if (count == 0)
    {
        freeaddrinfo(list);
        *out_os_error = 0;
        return MEL_NET_ERROR | MEL_NET_RESOLVE_FAILED;
    }

    Mel_Net_Address* items = mel_alloc_array(alloc, Mel_Net_Address, count);
    if (!items)
    {
        freeaddrinfo(list);
        return MEL_NET_ERROR;
    }

    usize i = 0;
    for (struct addrinfo* ai = list; ai; ai = ai->ai_next)
    {
        if (ai->ai_family != AF_INET && ai->ai_family != AF_INET6)
            continue;
        struct sockaddr_storage ss;
        memset(&ss, 0, sizeof ss);
        memcpy(&ss, ai->ai_addr, ai->ai_addrlen < sizeof ss ? ai->ai_addrlen : sizeof ss);
        addr_from_ss(&items[i], &ss);
        items[i].port = port;
        i++;
    }
    freeaddrinfo(list);

    *out_items = items;
    *out_count = i;
    return MEL_NET_OK;
}
