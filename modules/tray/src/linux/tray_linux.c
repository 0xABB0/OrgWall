#include <tray/provider.h>
#include <tray/linux/linux.h>
#include <allocator/allocator.h>
#include <collection.array/array.h>
#include <log/log.h>

#include "../tray_internal.h"

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

typedef struct GObject      GObject;
typedef struct GtkWidget    GtkWidget;
typedef struct GtkMenu      GtkMenu;
typedef struct GtkMenuItem  GtkMenuItem;
typedef struct GdkPixbuf    GdkPixbuf;
typedef struct AppIndicator AppIndicator;
typedef unsigned long       gulong;
typedef int                 gboolean;
typedef int                 gint;
typedef char                gchar;
typedef void*               gpointer;
typedef void (*GCallback)(void);
typedef void (*GDestroyNotify)(gpointer);

typedef gboolean (*fn_gtk_init_check)(int*, char***);
typedef GtkWidget* (*fn_gtk_menu_new)(void);
typedef GtkWidget* (*fn_gtk_menu_item_new_with_label)(const gchar*);
typedef GtkWidget* (*fn_gtk_check_menu_item_new_with_label)(const gchar*);
typedef GtkWidget* (*fn_gtk_separator_menu_item_new)(void);
typedef void (*fn_gtk_menu_shell_insert)(GtkWidget*, GtkWidget*, gint);
typedef void (*fn_gtk_menu_item_set_submenu)(GtkMenuItem*, GtkWidget*);
typedef void (*fn_gtk_menu_item_set_label)(GtkMenuItem*, const gchar*);
typedef void (*fn_gtk_widget_set_sensitive)(GtkWidget*, gboolean);
typedef void (*fn_gtk_widget_show)(GtkWidget*);
typedef void (*fn_gtk_widget_show_all)(GtkWidget*);
typedef void (*fn_gtk_widget_destroy)(GtkWidget*);
typedef void (*fn_gtk_check_menu_item_set_active)(GtkWidget*, gboolean);
typedef gboolean (*fn_gtk_check_menu_item_get_active)(GtkWidget*);
typedef void (*fn_gtk_check_menu_item_set_signal)(GtkWidget*, gboolean);
typedef void (*fn_gtk_container_remove)(GtkWidget*, GtkWidget*);

typedef gulong (*fn_g_signal_connect_data)(gpointer, const gchar*, GCallback, gpointer, GDestroyNotify, int);
typedef void (*fn_g_object_unref)(gpointer);
typedef GdkPixbuf* (*fn_gdk_pixbuf_new_from_data)(const unsigned char*, int, gboolean, int, int, int, int, void*, void*);

typedef AppIndicator* (*fn_app_indicator_new)(const gchar*, const gchar*, gint);
typedef void (*fn_app_indicator_set_status)(AppIndicator*, gint);
typedef void (*fn_app_indicator_set_menu)(AppIndicator*, GtkWidget*);
typedef void (*fn_app_indicator_set_icon_full)(AppIndicator*, const gchar*, const gchar*);
typedef void (*fn_app_indicator_set_title)(AppIndicator*, const gchar*);

#define APP_INDICATOR_CATEGORY_APPLICATION_STATUS 0
#define APP_INDICATOR_STATUS_ACTIVE               1
#define APP_INDICATOR_STATUS_PASSIVE              0
#define GTK_MENU_ITEM_CAST(w)                     ((GtkMenuItem*)(w))

typedef struct
{
    u64           token;
    AppIndicator* indicator;
    GtkWidget*    menu;
    char          id[64];
} Lin_Tray;

typedef struct
{
    u64        token;
    GtkWidget* menu;
} Lin_Menu;

typedef struct
{
    u64        token;
    GtkWidget* widget;
    GtkWidget* parent_menu;
    bool       checkbox;
    gulong     handler;
} Lin_Item;

static struct
{
    bool             ready;
    bool             ok;
    const Mel_Alloc* alloc;
    void*            gtk;
    void*            gobj;
    void*            gdk;
    void*            ind;
    u32              counter;

    fn_gtk_init_check                     gtk_init_check;
    fn_gtk_menu_new                       gtk_menu_new;
    fn_gtk_menu_item_new_with_label       gtk_menu_item_new_with_label;
    fn_gtk_check_menu_item_new_with_label gtk_check_menu_item_new_with_label;
    fn_gtk_separator_menu_item_new        gtk_separator_menu_item_new;
    fn_gtk_menu_shell_insert              gtk_menu_shell_insert;
    fn_gtk_menu_item_set_submenu          gtk_menu_item_set_submenu;
    fn_gtk_menu_item_set_label            gtk_menu_item_set_label;
    fn_gtk_widget_set_sensitive           gtk_widget_set_sensitive;
    fn_gtk_widget_show                    gtk_widget_show;
    fn_gtk_widget_show_all                gtk_widget_show_all;
    fn_gtk_widget_destroy                 gtk_widget_destroy;
    fn_gtk_check_menu_item_set_active     gtk_check_menu_item_set_active;
    fn_gtk_check_menu_item_get_active     gtk_check_menu_item_get_active;
    fn_gtk_container_remove               gtk_container_remove;
    fn_g_signal_connect_data              g_signal_connect_data;
    fn_g_object_unref                     g_object_unref;
    fn_gdk_pixbuf_new_from_data           gdk_pixbuf_new_from_data;
    fn_app_indicator_new                  app_indicator_new;
    fn_app_indicator_set_status           app_indicator_set_status;
    fn_app_indicator_set_menu             app_indicator_set_menu;
    fn_app_indicator_set_icon_full        app_indicator_set_icon_full;
    fn_app_indicator_set_title            app_indicator_set_title;

    Mel_Array(Lin_Tray) trays;
    Mel_Array(Lin_Menu) menus;
    Mel_Array(Lin_Item) items;
} l;

static void* sym(void* h, const char* name) { return h != NULL ? dlsym(h, name) : NULL; }

static void* open_first(const char* const* names)
{
    for (u32 i = 0; names[i] != NULL; i++)
    {
        void* h = dlopen(names[i], RTLD_LAZY | RTLD_GLOBAL);
        if (h != NULL)
            return h;
    }
    return NULL;
}

static bool load_libs(void)
{
    static const char* gtk_names[] = { "libgtk-3.so.0", "libgtk-3.so", NULL };
    static const char* gobj_names[] = { "libgobject-2.0.so.0", "libgobject-2.0.so", NULL };
    static const char* gdk_names[] = { "libgdk_pixbuf-2.0.so.0", "libgdk_pixbuf-2.0.so", NULL };
    static const char* ind_names[] = { "libayatana-appindicator3.so.1", "libappindicator3.so.1", "libayatana-appindicator3.so", "libappindicator3.so", NULL };

    l.gtk = open_first(gtk_names);
    l.gobj = open_first(gobj_names);
    l.gdk = open_first(gdk_names);
    l.ind = open_first(ind_names);
    if (l.gtk == NULL || l.gobj == NULL || l.ind == NULL)
        return false;

    l.gtk_init_check = (fn_gtk_init_check)sym(l.gtk, "gtk_init_check");
    l.gtk_menu_new = (fn_gtk_menu_new)sym(l.gtk, "gtk_menu_new");
    l.gtk_menu_item_new_with_label = (fn_gtk_menu_item_new_with_label)sym(l.gtk, "gtk_menu_item_new_with_label");
    l.gtk_check_menu_item_new_with_label = (fn_gtk_check_menu_item_new_with_label)sym(l.gtk, "gtk_check_menu_item_new_with_label");
    l.gtk_separator_menu_item_new = (fn_gtk_separator_menu_item_new)sym(l.gtk, "gtk_separator_menu_item_new");
    l.gtk_menu_shell_insert = (fn_gtk_menu_shell_insert)sym(l.gtk, "gtk_menu_shell_insert");
    l.gtk_menu_item_set_submenu = (fn_gtk_menu_item_set_submenu)sym(l.gtk, "gtk_menu_item_set_submenu");
    l.gtk_menu_item_set_label = (fn_gtk_menu_item_set_label)sym(l.gtk, "gtk_menu_item_set_label");
    l.gtk_widget_set_sensitive = (fn_gtk_widget_set_sensitive)sym(l.gtk, "gtk_widget_set_sensitive");
    l.gtk_widget_show = (fn_gtk_widget_show)sym(l.gtk, "gtk_widget_show");
    l.gtk_widget_show_all = (fn_gtk_widget_show_all)sym(l.gtk, "gtk_widget_show_all");
    l.gtk_widget_destroy = (fn_gtk_widget_destroy)sym(l.gtk, "gtk_widget_destroy");
    l.gtk_check_menu_item_set_active = (fn_gtk_check_menu_item_set_active)sym(l.gtk, "gtk_check_menu_item_set_active");
    l.gtk_check_menu_item_get_active = (fn_gtk_check_menu_item_get_active)sym(l.gtk, "gtk_check_menu_item_get_active");
    l.gtk_container_remove = (fn_gtk_container_remove)sym(l.gtk, "gtk_container_remove");
    l.g_signal_connect_data = (fn_g_signal_connect_data)sym(l.gobj, "g_signal_connect_data");
    l.g_object_unref = (fn_g_object_unref)sym(l.gobj, "g_object_unref");
    l.gdk_pixbuf_new_from_data = (fn_gdk_pixbuf_new_from_data)sym(l.gdk, "gdk_pixbuf_new_from_data");
    l.app_indicator_new = (fn_app_indicator_new)sym(l.ind, "app_indicator_new");
    l.app_indicator_set_status = (fn_app_indicator_set_status)sym(l.ind, "app_indicator_set_status");
    l.app_indicator_set_menu = (fn_app_indicator_set_menu)sym(l.ind, "app_indicator_set_menu");
    l.app_indicator_set_icon_full = (fn_app_indicator_set_icon_full)sym(l.ind, "app_indicator_set_icon_full");
    l.app_indicator_set_title = (fn_app_indicator_set_title)sym(l.ind, "app_indicator_set_title");

    return l.gtk_init_check != NULL && l.gtk_menu_new != NULL && l.gtk_menu_item_new_with_label != NULL && l.app_indicator_new != NULL && l.app_indicator_set_menu != NULL && l.g_signal_connect_data != NULL;
}

static Lin_Tray* tray_by_token(u64 token)
{
    for (usize i = 0; i < l.trays.count; i++)
        if (l.trays.items[i].token == token)
            return &l.trays.items[i];
    return NULL;
}

static Lin_Menu* menu_by_token(u64 token)
{
    for (usize i = 0; i < l.menus.count; i++)
        if (l.menus.items[i].token == token)
            return &l.menus.items[i];
    return NULL;
}

static Lin_Item* item_by_token(u64 token)
{
    for (usize i = 0; i < l.items.count; i++)
        if (l.items.items[i].token == token)
            return &l.items.items[i];
    return NULL;
}

static char* cstr(str8 s)
{
    char* buf = mel_alloc(l.alloc, s.len + 1);
    if (s.len > 0 && s.data != NULL)
        memcpy(buf, s.data, s.len);
    buf[s.len] = 0;
    return buf;
}

static void on_activate(GObject* obj, gpointer user)
{
    (void)obj;
    u64 token = (u64)(usize)user;
    mel_tray__dispatch_item_clicked(token);
}

static bool lin_supported(void* user)
{
    (void)user;
    return l.ok;
}

static Mel_Tray_Status lin_create(void* user, const Mel_Tray_Lowered* lowered)
{
    (void)user;
    Lin_Menu* lm = menu_by_token(lowered->menu_token);
    Lin_Tray  lt = { .token = lowered->token };
    snprintf(lt.id, sizeof lt.id, "melody-tray-%u", ++l.counter);

    const char* icon_name = "application-default-icon";
    lt.indicator = l.app_indicator_new(lt.id, icon_name, APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
    if (lt.indicator == NULL)
    {
        mel_log_error("tray", "app_indicator_new returned NULL");
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_BACKEND_FAIL;
    }
    l.app_indicator_set_status(lt.indicator, lowered->visible ? APP_INDICATOR_STATUS_ACTIVE : APP_INDICATOR_STATUS_PASSIVE);
    if (lm != NULL)
    {
        lt.menu = lm->menu;
        l.gtk_widget_show_all(lm->menu);
        l.app_indicator_set_menu(lt.indicator, lm->menu);
    }
    if (lowered->title.len > 0 && l.app_indicator_set_title != NULL)
    {
        char* title = cstr(lowered->title);
        l.app_indicator_set_title(lt.indicator, title);
        mel_dealloc(l.alloc, title);
    }
    mel_array_push(&l.trays, lt);
    return lowered->image.path.len == 0 && lowered->image.rgba == NULL ? (MEL_TRAY_WARNED | MEL_TRAY_WARN_IMAGE_RESCALED) : MEL_TRAY_OK;
}

static void lin_destroy(void* user, u64 token)
{
    (void)user;
    Lin_Tray* lt = tray_by_token(token);
    if (lt == NULL)
        return;
    l.app_indicator_set_status(lt->indicator, APP_INDICATOR_STATUS_PASSIVE);
    l.g_object_unref(lt->indicator);
    usize idx = (usize)(lt - l.trays.items);
    mel_array_remove_unordered(&l.trays, idx);
}

static Mel_Tray_Status lin_set_image(void* user, u64 token, Mel_Tray_Image image)
{
    (void)user;
    Lin_Tray* lt = tray_by_token(token);
    if (lt == NULL)
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_BACKEND_FAIL;
    if (image.path.len > 0 && image.path.data != NULL && l.app_indicator_set_icon_full != NULL)
    {
        char* path = cstr(image.path);
        l.app_indicator_set_icon_full(lt->indicator, path, "");
        mel_dealloc(l.alloc, path);
        return MEL_TRAY_OK;
    }
    return MEL_TRAY_WARNED | MEL_TRAY_WARN_IMAGE_RESCALED;
}

static Mel_Tray_Status lin_set_tooltip(void* user, u64 token, str8 tooltip)
{
    (void)user;
    (void)token;
    (void)tooltip;
    return MEL_TRAY_WARNED | MEL_TRAY_WARN_TOOLTIP_DROPPED;
}

static Mel_Tray_Status lin_set_title(void* user, u64 token, str8 title)
{
    (void)user;
    Lin_Tray* lt = tray_by_token(token);
    if (lt == NULL || l.app_indicator_set_title == NULL)
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_BACKEND_FAIL;
    char* t = cstr(title);
    l.app_indicator_set_title(lt->indicator, t);
    mel_dealloc(l.alloc, t);
    return MEL_TRAY_OK;
}

static Mel_Tray_Status lin_set_visible(void* user, u64 token, bool visible)
{
    (void)user;
    Lin_Tray* lt = tray_by_token(token);
    if (lt == NULL)
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_BACKEND_FAIL;
    l.app_indicator_set_status(lt->indicator, visible ? APP_INDICATOR_STATUS_ACTIVE : APP_INDICATOR_STATUS_PASSIVE);
    return MEL_TRAY_OK;
}

static Mel_Tray_Status lin_menu_create(void* user, u64 menu_token)
{
    (void)user;
    Lin_Menu lm = { .token = menu_token, .menu = l.gtk_menu_new() };
    if (lm.menu == NULL)
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_BACKEND_FAIL;
    mel_array_push(&l.menus, lm);
    return MEL_TRAY_OK;
}

static void lin_menu_destroy(void* user, u64 menu_token)
{
    (void)user;
    Lin_Menu* lm = menu_by_token(menu_token);
    if (lm == NULL)
        return;
    usize idx = (usize)(lm - l.menus.items);
    mel_array_remove_unordered(&l.menus, idx);
}

static Mel_Tray_Status lin_item_add(void* user, const Mel_Tray_Item_Lowered* lowered)
{
    (void)user;
    Lin_Menu* parent = menu_by_token(lowered->parent_menu_token);
    if (parent == NULL)
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_BACKEND_FAIL;

    Lin_Item it = { .token = lowered->token, .parent_menu = parent->menu };
    char*    label = cstr(lowered->label);
    if ((lowered->flags & MEL_TRAY_ITEM_SEPARATOR) != 0)
    {
        it.widget = l.gtk_separator_menu_item_new();
    }
    else if ((lowered->flags & MEL_TRAY_ITEM_CHECKBOX) != 0)
    {
        it.widget = l.gtk_check_menu_item_new_with_label(label);
        it.checkbox = true;
        l.gtk_check_menu_item_set_active(it.widget, (lowered->flags & MEL_TRAY_ITEM_CHECKED) != 0);
    }
    else
    {
        it.widget = l.gtk_menu_item_new_with_label(label);
    }
    mel_dealloc(l.alloc, label);
    if (it.widget == NULL)
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_BACKEND_FAIL;

    if ((lowered->flags & MEL_TRAY_ITEM_SEPARATOR) == 0)
    {
        l.gtk_widget_set_sensitive(it.widget, (lowered->flags & MEL_TRAY_ITEM_ENABLED) != 0);
        if (lowered->submenu_token != 0)
        {
            Lin_Menu* sub = menu_by_token(lowered->submenu_token);
            if (sub != NULL)
                l.gtk_menu_item_set_submenu(GTK_MENU_ITEM_CAST(it.widget), sub->menu);
        }
        else
        {
            it.handler = l.g_signal_connect_data(it.widget, "activate", (GCallback)on_activate, (gpointer)(usize)lowered->token, NULL, 0);
        }
    }
    l.gtk_menu_shell_insert(parent->menu, it.widget, (gint)lowered->at);
    l.gtk_widget_show(it.widget);
    mel_array_push(&l.items, it);
    return MEL_TRAY_OK;
}

static void lin_item_remove(void* user, u64 token)
{
    (void)user;
    Lin_Item* it = item_by_token(token);
    if (it == NULL)
        return;
    if (it->parent_menu != NULL && it->widget != NULL)
        l.gtk_container_remove(it->parent_menu, it->widget);
    usize idx = (usize)(it - l.items.items);
    mel_array_remove_unordered(&l.items, idx);
}

static Mel_Tray_Status lin_item_set_label(void* user, u64 token, str8 label)
{
    (void)user;
    Lin_Item* it = item_by_token(token);
    if (it == NULL || it->widget == NULL)
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_BACKEND_FAIL;
    char* s = cstr(label);
    l.gtk_menu_item_set_label(GTK_MENU_ITEM_CAST(it->widget), s);
    mel_dealloc(l.alloc, s);
    return MEL_TRAY_OK;
}

static Mel_Tray_Status lin_item_set_flags(void* user, u64 token, Mel_Tray_Item_Flags flags)
{
    (void)user;
    Lin_Item* it = item_by_token(token);
    if (it == NULL || it->widget == NULL)
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_BACKEND_FAIL;
    l.gtk_widget_set_sensitive(it->widget, (flags & MEL_TRAY_ITEM_ENABLED) != 0);
    if (it->checkbox)
        l.gtk_check_menu_item_set_active(it->widget, (flags & MEL_TRAY_ITEM_CHECKED) != 0);
    return MEL_TRAY_OK;
}

static void* lin_native(void* user, u64 token)
{
    (void)user;
    Lin_Tray* lt = tray_by_token(token);
    return lt != NULL ? (void*)lt->indicator : NULL;
}

void mel_tray__register_host_providers(void)
{
    if (!l.ready)
    {
        l.alloc = mel_tray__alloc();
        mel_array_init(&l.trays, l.alloc);
        mel_array_init(&l.menus, l.alloc);
        mel_array_init(&l.items, l.alloc);
        l.ready = true;
        if (load_libs())
        {
            int argc = 0;
            l.ok = l.gtk_init_check(&argc, NULL) != 0;
            if (!l.ok)
                mel_log_warn("tray", "gtk_init_check failed (no display); tray honest-absent");
        }
        else
        {
            mel_log_warn("tray", "libappindicator/gtk3 not present; tray honest-absent");
        }
    }
    if (!l.ok)
        return;

    static const Mel_Tray_Provider_Desc desc = {
        .name = "linux-appindicator",
        .supported = lin_supported,
        .create = lin_create,
        .destroy = lin_destroy,
        .set_image = lin_set_image,
        .set_tooltip = lin_set_tooltip,
        .set_title = lin_set_title,
        .set_visible = lin_set_visible,
        .menu_create = lin_menu_create,
        .menu_destroy = lin_menu_destroy,
        .item_add = lin_item_add,
        .item_remove = lin_item_remove,
        .item_set_label = lin_item_set_label,
        .item_set_flags = lin_item_set_flags,
        .native = lin_native,
    };
    mel_tray_provider_register(&desc);
}

const char* mel_tray_linux_bus_name(Mel_Tray t)
{
    Lin_Tray* lt = tray_by_token(mel_slotmap_handle_pack64(t.h));
    return lt != NULL ? lt->id : "";
}

const char* mel_tray_linux_object_path(Mel_Tray t)
{
    (void)t;
    return "/org/ayatana/NotificationItem";
}
