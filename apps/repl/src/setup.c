#include <core/types.h>

#include <allocator/allocator.h>
#include <boot/boot.h>
#include <collection/array.h>
#include <collection/list.h>
#include <executor/executor.h>
#include <future/future.h>
#include <port/port.h>
#include <string/str8.h>
#include <vat/vat.h>

#include <clang/clang.h>
#include <repl/repl.h>

#include <stdio.h>

#define CHUNK_LEN ((usize)4096)

typedef struct
{
    const Mel_Alloc* alloc;
    Mel_Vat*         vat;
    Mel_Repl*        repl;
    Mel_Repl_Drive*  drive;
    Mel_Port*        port;
    u8*              chunk;
    Mel_Array(u8) line;
    Mel_Task    on_chunk;
    Mel_Future* pending;
} App;

static App g_app;

static void stdout_write(void* self, str8 bytes)
{
    (void)self;
    fwrite(bytes.data, 1, (size_t)bytes.len, stdout);
    fflush(stdout);
}

static void app_quit(App* app, int code)
{
    mel_app_set_exit_code(code);
    mel_vat_quit(app->vat);
}

static void next_read(App* app)
{
    Mel_Future* f = mel_port_read(app->port, .fd = 0, .buffer = app->chunk, .len = CHUNK_LEN);
    if (!f)
    {
        fputs("repl: stdin read submission failed\n", stderr);
        app_quit(app, 1);
        return;
    }
    app->pending = f;
    mel_future_then(f, &app->on_chunk, mel_vat_executor(app->vat));
}

static void feed_line(App* app)
{
    str8 line = str8_from_parts(app->line.items, (size)app->line.count);
    mel_repl_drive_line(app->drive, line);
    mel_array_clear(&app->line);
}

static void on_chunk(Mel_Task* task)
{
    App* app = mel_container_of(task, App, on_chunk);

    Mel_Port_Result r = *mel_port_future_result(app->pending);
    mel_port_future_release(app->pending);
    app->pending = NULL;

    if (mel_port_status_failed(r.status))
    {
        fputs("repl: stdin read failed\n", stderr);
        app_quit(app, 1);
        return;
    }

    for (usize i = 0; i < r.bytes_transferred; i++)
    {
        u8 c = app->chunk[i];
        if (c == '\n')
            feed_line(app);
        else
            mel_array_push(&app->line, c);
    }

    if (mel_port_status_eof(r.status))
    {
        if (app->line.count > 0)
            feed_line(app);
        mel_repl_drive_destroy(app->drive);
        app->drive = NULL;
        app_quit(app, 0);
        return;
    }

    next_read(app);
}

static void app_teardown(void* user)
{
    App* app = (App*)user;
    if (app->drive)
    {
        mel_repl_drive_destroy(app->drive);
        app->drive = NULL;
    }
    if (app->port)
    {
        mel_port_destroy(app->port);
        app->port = NULL;
    }
    mel_array_free(&app->line);
    if (app->chunk)
    {
        mel_dealloc(app->alloc, app->chunk);
        app->chunk = NULL;
    }
    if (app->repl)
    {
        mel_repl_destroy(app->repl);
        app->repl = NULL;
    }
}

void mel_app_setup(Mel_Vat* root)
{
    const Mel_Alloc* a = mel_vat_alloc(root);

    Mel_Repl_Lang lang = mel_clang_repl_lang(a);
    if (!lang.eval)
    {
        fputs("repl: failed to create the C interpreter\n", stderr);
        mel_app_set_exit_code(1);
        return;
    }

    Mel_Repl* repl = mel_repl_create(a, lang);
    if (!repl)
    {
        fputs("repl: failed to create the REPL\n", stderr);
        if (lang.destroy)
            lang.destroy(lang.self);
        mel_app_set_exit_code(1);
        return;
    }

    g_app.alloc = a;
    g_app.vat = root;
    g_app.repl = repl;
    mel_array_init(&g_app.line, a);
    mel_task_init(&g_app.on_chunk, on_chunk);

    g_app.port = mel_port_create(.vat = root, .alloc = a);
    if (!g_app.port || !mel_port_available(g_app.port))
    {
        fputs("repl: async stdin is unavailable on this platform\n", stderr);
        app_teardown(&g_app);
        mel_app_set_exit_code(1);
        return;
    }

    g_app.chunk = (u8*)mel_alloc(a, CHUNK_LEN);
    if (!g_app.chunk)
    {
        app_teardown(&g_app);
        mel_app_set_exit_code(1);
        return;
    }

    fputs("melody C repl — LLVM ORC JIT. Enter C expressions or declarations; Ctrl-D to exit.\n", stdout);
    fflush(stdout);

    Mel_Repl_Sink    sink = { NULL, stdout_write, NULL };
    Mel_Repl_Prompts prompts = { S8("» "), S8("… ") };
    g_app.drive = mel_repl_drive_create(repl, sink, prompts);
    if (!g_app.drive)
    {
        app_teardown(&g_app);
        mel_app_set_exit_code(1);
        return;
    }

    mel_vat_retain(root);
    mel_app_on_exit(app_teardown, &g_app);
    next_read(&g_app);
}
