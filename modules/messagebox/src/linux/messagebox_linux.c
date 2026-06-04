#include <messagebox/backend.h>
#include <log/log.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

typedef struct GtkWidget         GtkWidget;
typedef struct GtkWindow         GtkWindow;
typedef struct GtkDialog         GtkDialog;
typedef struct GtkCssProvider    GtkCssProvider;
typedef struct GtkStyleContext   GtkStyleContext;
typedef struct GdkScreen         GdkScreen;
typedef int                      gboolean;
typedef unsigned long            gulong;

#define GTK_DIR_LTR 0
#define GTK_DIR_RTL 1

#define GTK_MESSAGE_INFO    0
#define GTK_MESSAGE_WARNING 1
#define GTK_MESSAGE_ERROR   3
#define GTK_BUTTONS_NONE    0
#define GTK_STYLE_PROVIDER_PRIORITY_APPLICATION 600u

typedef struct
{
    void* lib;
    gboolean (*init_check)(int*, char***);
    GtkWidget* (*message_dialog_new)(GtkWindow*, int, int, int, const char*, ...);
    void (*window_set_title)(GtkWindow*, const char*);
    GtkWidget* (*dialog_add_button)(GtkDialog*, const char*, int);
    void (*dialog_set_default_response)(GtkDialog*, int);
    int (*dialog_run)(GtkDialog*);
    void (*widget_destroy)(GtkWidget*);
    void (*widget_set_direction)(GtkWidget*, int);
    GtkStyleContext* (*widget_get_style_context)(GtkWidget*);
    GtkCssProvider* (*css_provider_new)(void);
    gboolean (*css_provider_load_from_data)(GtkCssProvider*, const char*, long, void*);
    void (*style_context_add_provider_for_screen)(GdkScreen*, void*, unsigned);
    GdkScreen* (*widget_get_screen)(GtkWidget*);
    void (*main_iteration)(void);
    gboolean (*events_pending)(void);
    void (*object_unref)(void*);
} Gtk;

static Gtk g;
static bool g_loaded;
static bool g_load_failed;

static void* sym(const char* name)
{
    void* s = dlsym(g.lib, name);
    if (!s)
        mel_log_error("messagebox", "gtk backend: symbol '%s' missing", name);
    return s;
}

static bool load(void)
{
    if (g_loaded)
        return true;
    if (g_load_failed)
        return false;

    g.lib = dlopen("libgtk-3.so.0", RTLD_NOW | RTLD_GLOBAL);
    if (!g.lib)
        g.lib = dlopen("libgtk-3.so", RTLD_NOW | RTLD_GLOBAL);
    if (!g.lib)
    {
        mel_log_error("messagebox", "gtk backend: cannot dlopen libgtk-3.so.0 (%s)", dlerror());
        g_load_failed = true;
        return false;
    }

    g.init_check = (gboolean (*)(int*, char***))sym("gtk_init_check");
    g.message_dialog_new = (GtkWidget * (*)(GtkWindow*, int, int, int, const char*, ...)) sym("gtk_message_dialog_new");
    g.window_set_title = (void (*)(GtkWindow*, const char*))sym("gtk_window_set_title");
    g.dialog_add_button = (GtkWidget * (*)(GtkDialog*, const char*, int)) sym("gtk_dialog_add_button");
    g.dialog_set_default_response = (void (*)(GtkDialog*, int))sym("gtk_dialog_set_default_response");
    g.dialog_run = (int (*)(GtkDialog*))sym("gtk_dialog_run");
    g.widget_destroy = (void (*)(GtkWidget*))sym("gtk_widget_destroy");
    g.widget_set_direction = (void (*)(GtkWidget*, int))sym("gtk_widget_set_direction");
    g.widget_get_style_context = (GtkStyleContext * (*)(GtkWidget*)) sym("gtk_widget_get_style_context");
    g.css_provider_new = (GtkCssProvider * (*)(void)) sym("gtk_css_provider_new");
    g.css_provider_load_from_data = (gboolean (*)(GtkCssProvider*, const char*, long, void*))sym("gtk_css_provider_load_from_data");
    g.style_context_add_provider_for_screen = (void (*)(GdkScreen*, void*, unsigned))sym("gtk_style_context_add_provider_for_screen");
    g.widget_get_screen = (GdkScreen * (*)(GtkWidget*)) sym("gtk_widget_get_screen");
    g.main_iteration = (void (*)(void))sym("gtk_main_iteration");
    g.events_pending = (gboolean (*)(void))sym("gtk_events_pending");
    g.object_unref = (void (*)(void*))sym("g_object_unref");

    if (!g.init_check || !g.message_dialog_new || !g.dialog_add_button || !g.dialog_run || !g.widget_destroy)
    {
        g_load_failed = true;
        return false;
    }

    if (!g.init_check(NULL, NULL))
    {
        mel_log_error("messagebox", "gtk backend: gtk_init_check failed (no display?)");
        g_load_failed = true;
        return false;
    }

    g_loaded = true;
    return true;
}

bool mel_msgbox__plat_available(void) { return load(); }

static const char* cstr_or(const Mel_Alloc* a, str8 s, const char* fallback)
{
    if (s.len <= 0 || !s.data)
        return fallback;
    char* c = (char*)mel_alloc(a, (usize)s.len + 1);
    if (!c)
        return fallback;
    memcpy(c, s.data, (usize)s.len);
    c[s.len] = 0;
    return c;
}

static int gtk_type_for(Mel_Msgbox_Severity sev)
{
    switch (sev)
    {
        case MEL_MSGBOX_SEVERITY_WARN:  return GTK_MESSAGE_WARNING;
        case MEL_MSGBOX_SEVERITY_ERROR: return GTK_MESSAGE_ERROR;
        default:                        return GTK_MESSAGE_INFO;
    }
}

static void apply_colors(const Mel_Msgbox_Request* req, GtkWidget* dialog)
{
    if (!req->accent.has_value && !req->text.has_value && !req->background.has_value)
        return;
    if (!g.css_provider_new || !g.css_provider_load_from_data || !g.style_context_add_provider_for_screen || !g.widget_get_screen)
        return;

    char css[512];
    int  n = snprintf(css, sizeof css, "dialog {");
    if (req->background.has_value)
        n += snprintf(css + n, (size_t)(sizeof css - n), " background-color: rgba(%u,%u,%u,%f);", req->background.value.r, req->background.value.g, req->background.value.b, req->background.value.a / 255.0);
    if (req->text.has_value)
        n += snprintf(css + n, (size_t)(sizeof css - n), " color: rgba(%u,%u,%u,%f);", req->text.value.r, req->text.value.g, req->text.value.b, req->text.value.a / 255.0);
    n += snprintf(css + n, (size_t)(sizeof css - n), " }");
    if (req->accent.has_value)
        snprintf(css + n, (size_t)(sizeof css - n), " button:default { background: rgba(%u,%u,%u,%f); }", req->accent.value.r, req->accent.value.g, req->accent.value.b, req->accent.value.a / 255.0);

    GtkCssProvider* prov = g.css_provider_new();
    if (!prov)
        return;
    g.css_provider_load_from_data(prov, css, -1, NULL);
    GdkScreen* screen = g.widget_get_screen(dialog);
    if (screen)
        g.style_context_add_provider_for_screen(screen, prov, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    if (g.object_unref)
        g.object_unref(prov);
}

Mel_Msgbox_Status mel_msgbox__plat_show(const Mel_Msgbox_Request* req, i32* out_chosen_id)
{
    if (!load())
    {
        *out_chosen_id = req->escape_id;
        return MEL_MSGBOX_ERROR | MEL_MSGBOX_RESULT_NO_BACKEND;
    }

    const Mel_Alloc* a = mel_alloc_heap();
    const char*      title = cstr_or(a, req->title, "");
    const char*      message = cstr_or(a, req->message, "");

    GtkWidget* dialog = g.message_dialog_new(NULL, 0, gtk_type_for(req->severity), GTK_BUTTONS_NONE, "%s", title[0] ? title : message);
    if (!dialog)
    {
        *out_chosen_id = req->escape_id;
        return MEL_MSGBOX_ERROR;
    }
    if (title[0] && g.window_set_title)
        g.window_set_title((GtkWindow*)dialog, title);

    int default_response = 0;
    for (u32 i = 0; i < req->button_count; i++)
    {
        const char* label = cstr_or(a, req->buttons[i].label, "OK");
        g.dialog_add_button((GtkDialog*)dialog, label, (int)(i + 1));
        if (req->buttons[i].id == req->default_id)
            default_response = (int)(i + 1);
    }
    if (default_response && g.dialog_set_default_response)
        g.dialog_set_default_response((GtkDialog*)dialog, default_response);

    if (req->right_to_left && g.widget_set_direction)
        g.widget_set_direction(dialog, GTK_DIR_RTL);

    apply_colors(req, dialog);

    int response = g.dialog_run((GtkDialog*)dialog);
    g.widget_destroy(dialog);
    if (g.events_pending && g.main_iteration)
        while (g.events_pending())
            g.main_iteration();

    i32               chosen = req->escape_id;
    Mel_Msgbox_Status st = MEL_MSGBOX_OK;
    if (response >= 1 && (u32)response <= req->button_count)
        chosen = req->buttons[response - 1].id;
    else
        st |= MEL_MSGBOX_RESULT_DISMISSED;

    *out_chosen_id = chosen;
    return st;
}
