#include "dialog_linux.h"

#include <dialog/backend.h>
#include <window/window.h>
#include <log/log.h>

#include <allocator/allocator.h>

#include <dlfcn.h>
#include <string.h>

typedef struct GtkWidget    GtkWidget;
typedef struct GtkFileFilter GtkFileFilter;
typedef int                 gint;
typedef int                 gboolean;
typedef void*               gpointer;
typedef char                gchar;
typedef struct GSList       GSList;

#define GTK_FILE_CHOOSER_ACTION_OPEN          0
#define GTK_FILE_CHOOSER_ACTION_SAVE          1
#define GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER 2
#define GTK_RESPONSE_ACCEPT                   (-3)
#define GTK_RESPONSE_CANCEL                   (-6)

struct GSList
{
    gpointer data;
    GSList*  next;
};

typedef struct
{
    void* lib;
    gboolean (*gtk_init_check)(int*, char***);
    GtkWidget* (*gtk_file_chooser_dialog_new)(const gchar*, void*, gint, const gchar*, ...);
    void (*gtk_file_chooser_set_select_multiple)(GtkWidget*, gboolean);
    void (*gtk_file_chooser_set_current_name)(GtkWidget*, const gchar*);
    void (*gtk_file_chooser_set_current_folder)(GtkWidget*, const gchar*);
    void (*gtk_file_chooser_add_filter)(GtkWidget*, GtkFileFilter*);
    GtkFileFilter* (*gtk_file_filter_new)(void);
    void (*gtk_file_filter_set_name)(GtkFileFilter*, const gchar*);
    void (*gtk_file_filter_add_pattern)(GtkFileFilter*, const gchar*);
    GtkFileFilter* (*gtk_file_chooser_get_filter)(GtkWidget*);
    const gchar* (*gtk_file_filter_get_name)(GtkFileFilter*);
    GSList* (*gtk_file_chooser_get_filenames)(GtkWidget*);
    gchar* (*gtk_file_chooser_get_filename)(GtkWidget*);
    gint (*gtk_dialog_run)(GtkWidget*);
    void (*gtk_widget_destroy)(GtkWidget*);
    gboolean (*gtk_events_pending)(void);
    gboolean (*gtk_main_iteration)(void);
    void (*g_free)(gpointer);
    void (*g_slist_free)(GSList*);
} Gtk;

static Gtk    g_gtk;
static bool   g_gtk_loaded;
static bool   g_gtk_ok;

#define LOAD(sym) g_gtk.sym = (typeof(g_gtk.sym))dlsym(g_gtk.lib, #sym)

static bool gtk_load(void)
{
    if (g_gtk_loaded)
        return g_gtk_ok;
    g_gtk_loaded = true;
    g_gtk.lib = dlopen("libgtk-3.so.0", RTLD_LAZY | RTLD_LOCAL);
    if (!g_gtk.lib)
        return false;
    LOAD(gtk_init_check);
    LOAD(gtk_file_chooser_dialog_new);
    LOAD(gtk_file_chooser_set_select_multiple);
    LOAD(gtk_file_chooser_set_current_name);
    LOAD(gtk_file_chooser_set_current_folder);
    LOAD(gtk_file_chooser_add_filter);
    LOAD(gtk_file_filter_new);
    LOAD(gtk_file_filter_set_name);
    LOAD(gtk_file_filter_add_pattern);
    LOAD(gtk_file_chooser_get_filter);
    LOAD(gtk_file_filter_get_name);
    LOAD(gtk_file_chooser_get_filenames);
    LOAD(gtk_file_chooser_get_filename);
    LOAD(gtk_dialog_run);
    LOAD(gtk_widget_destroy);
    LOAD(gtk_events_pending);
    LOAD(gtk_main_iteration);
    void* glib = dlopen("libglib-2.0.so.0", RTLD_LAZY | RTLD_LOCAL);
    g_gtk.g_free = glib ? (typeof(g_gtk.g_free))dlsym(glib, "g_free") : NULL;
    g_gtk.g_slist_free = glib ? (typeof(g_gtk.g_slist_free))dlsym(glib, "g_slist_free") : NULL;

    if (!g_gtk.gtk_file_chooser_dialog_new || !g_gtk.gtk_dialog_run || !g_gtk.gtk_init_check)
        return false;
    if (!g_gtk.gtk_init_check(NULL, NULL))
        return false;
    g_gtk_ok = true;
    return true;
}

static void add_filters(GtkWidget* dlg, Mel_Dialog_Job* job)
{
    u32 fc = mel_dialog_job_filter_count(job);
    for (u32 i = 0; i < fc; i++)
    {
        GtkFileFilter* f = g_gtk.gtk_file_filter_new();
        const char*    label = mel_dialog_job_filter_label(job, i);
        g_gtk.gtk_file_filter_set_name(f, label ? label : "Files");
        u32 pc = mel_dialog_job_filter_pattern_count(job, i);
        for (u32 p = 0; p < pc; p++)
        {
            const char* pat = mel_dialog_job_filter_pattern(job, i, p);
            if (!pat)
                continue;
            char        glob[280];
            const char* ext = strrchr(pat, '.');
            if (strchr(pat, '*'))
                snprintf(glob, sizeof glob, "%s", pat);
            else if (ext && ext[1])
                snprintf(glob, sizeof glob, "*%s", ext);
            else
                snprintf(glob, sizeof glob, "*.%s", pat);
            g_gtk.gtk_file_filter_add_pattern(f, glob);
        }
        g_gtk.gtk_file_chooser_add_filter(dlg, f);
    }
}

bool mel_dialog__gtk_run(Mel_Dialog_Job* job)
{
    if (!gtk_load())
        return false;

    u32  request = mel_dialog_job_request(job);
    gint action = GTK_FILE_CHOOSER_ACTION_OPEN;
    if (request & MEL_DIALOG_REQUEST_SAVE_FILE)
        action = GTK_FILE_CHOOSER_ACTION_SAVE;
    else if (request & MEL_DIALOG_REQUEST_OPEN_DIR)
        action = GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;

    const char* title = mel_dialog_job_title(job);
    GtkWidget*  dlg = g_gtk.gtk_file_chooser_dialog_new(title ? title : "Select", NULL, action,
                                                       "_Cancel", GTK_RESPONSE_CANCEL,
                                                       (request & MEL_DIALOG_REQUEST_SAVE_FILE) ? "_Save" : "_Open", GTK_RESPONSE_ACCEPT,
                                                       (const char*)NULL);
    if (!dlg)
        return false;

    if (request & MEL_DIALOG_REQUEST_MULTI)
        g_gtk.gtk_file_chooser_set_select_multiple(dlg, 1);
    const char* dir = mel_dialog_job_default_path(job);
    if (dir)
        g_gtk.gtk_file_chooser_set_current_folder(dlg, dir);
    const char* name = mel_dialog_job_default_name(job);
    if (name && (request & MEL_DIALOG_REQUEST_SAVE_FILE))
        g_gtk.gtk_file_chooser_set_current_name(dlg, name);
    if (!(request & MEL_DIALOG_REQUEST_OPEN_DIR))
        add_filters(dlg, job);

    gint resp = g_gtk.gtk_dialog_run(dlg);
    Mel_Dialog_Status base = MEL_DIALOG_OK;
    if (resp == GTK_RESPONSE_ACCEPT)
    {
        if ((request & MEL_DIALOG_REQUEST_MULTI) && g_gtk.gtk_file_chooser_get_filenames)
        {
            GSList* list = g_gtk.gtk_file_chooser_get_filenames(dlg);
            for (GSList* it = list; it; it = it->next)
            {
                if (it->data)
                {
                    mel_dialog_job_emit_path(job, (const char*)it->data);
                    if (g_gtk.g_free)
                        g_gtk.g_free(it->data);
                }
            }
            if (g_gtk.g_slist_free)
                g_gtk.g_slist_free(list);
        }
        else
        {
            gchar* fn = g_gtk.gtk_file_chooser_get_filename(dlg);
            if (fn)
            {
                mel_dialog_job_emit_path(job, fn);
                if (g_gtk.g_free)
                    g_gtk.g_free(fn);
            }
        }
        GtkFileFilter* chosen = g_gtk.gtk_file_chooser_get_filter ? g_gtk.gtk_file_chooser_get_filter(dlg) : NULL;
        if (chosen && g_gtk.gtk_file_filter_get_name)
        {
            const char* cn = g_gtk.gtk_file_filter_get_name(chosen);
            u32         fc = mel_dialog_job_filter_count(job);
            for (u32 i = 0; i < fc; i++)
            {
                const char* lbl = mel_dialog_job_filter_label(job, i);
                if (cn && lbl && strcmp(cn, lbl) == 0)
                {
                    mel_dialog_job_set_chosen_filter(job, i);
                    break;
                }
            }
        }
    }
    else
    {
        base = MEL_DIALOG_OK | MEL_DIALOG_CANCELLED;
    }

    g_gtk.gtk_widget_destroy(dlg);
    while (g_gtk.gtk_events_pending && g_gtk.gtk_events_pending())
        g_gtk.gtk_main_iteration();

    mel_dialog_job_resolve(job, base);
    return true;
}
