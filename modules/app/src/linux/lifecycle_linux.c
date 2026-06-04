#include <core/platform.h>

#if !MEL_PLATFORM_LINUX
#error "linux-only translation unit"
#endif

#include <app/provider.h>
#include <reactor/reactor.h>
#include <log/log.h>

#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

typedef struct
{
    int                 pipe_read;
    int                 pipe_write;
    Mel_Reactor_Source* source;
    Mel_Reactor_Poll    poll;
    struct sigaction    prev_term;
    struct sigaction    prev_int;
    bool                installed;
} Linux_Lifecycle;

static Linux_Lifecycle g_lx;

static void signal_handler(int signo)
{
    (void)signo;
    unsigned char byte = (unsigned char)1;
    ssize_t       n = write(g_lx.pipe_write, &byte, 1);
    (void)n;
}

static bool source_check(Mel_Reactor_Source* s)
{
    (void)s;
    return (g_lx.poll.revents & (MEL_REACTOR_POLL_IN | MEL_REACTOR_POLL_ERR | MEL_REACTOR_POLL_HUP)) != 0;
}

static bool source_dispatch(Mel_Reactor_Source* s, Mel_Reactor_Source_Proc callback, void* user)
{
    (void)s;
    (void)callback;
    (void)user;
    unsigned char buf[16];
    ssize_t       got;
    while ((got = read(g_lx.pipe_read, buf, sizeof buf)) > 0)
        ;
    (void)got;
    mel_app__emit(MEL_APP_PHASE_WILL_TERMINATE);
    return true;
}

static const Mel_Reactor_Source_Callbacks LX_VT = {
    .check = source_check,
    .dispatch = source_dispatch,
};

static void set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1)
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void plat_start(void* user)
{
    (void)user;
    if (g_lx.installed)
        return;
    Mel_Reactor* reactor = mel_app__reactor();
    if (reactor == NULL)
    {
        mel_log_warn("app", "linux lifecycle: no reactor; SIGTERM/SIGINT not wired");
        return;
    }
    int fds[2];
    if (pipe(fds) != 0)
    {
        mel_log_error("app", "linux lifecycle: pipe() failed; SIGTERM/SIGINT not wired");
        return;
    }
    g_lx.pipe_read = fds[0];
    g_lx.pipe_write = fds[1];
    set_nonblock(g_lx.pipe_read);
    set_nonblock(g_lx.pipe_write);

    g_lx.poll.handle = g_lx.pipe_read;
    g_lx.poll.events = MEL_REACTOR_POLL_IN;
    g_lx.poll.revents = 0;

    g_lx.source = mel_reactor_source_new(&LX_VT, sizeof(Mel_Reactor_Source));
    mel_reactor_source_set_callback(g_lx.source, NULL, NULL);
    mel_reactor_source_add_poll(g_lx.source, &g_lx.poll);
    mel_reactor_source_attach(reactor, g_lx.source);

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGTERM, &sa, &g_lx.prev_term);
    sigaction(SIGINT, &sa, &g_lx.prev_int);

    g_lx.installed = true;
}

static void plat_stop(void* user)
{
    (void)user;
    if (!g_lx.installed)
        return;
    sigaction(SIGTERM, &g_lx.prev_term, NULL);
    sigaction(SIGINT, &g_lx.prev_int, NULL);
    if (g_lx.source != NULL)
        mel_reactor_source_destroy(g_lx.source);
    close(g_lx.pipe_read);
    close(g_lx.pipe_write);
    memset(&g_lx, 0, sizeof g_lx);
}

void mel_app__register_platform_provider(void)
{
    Mel_App_Provider_Desc desc = { .name = "linux-signals", .start = plat_start, .stop = plat_stop };
    mel_app_provider_register(&desc);
}
