#import <AppKit/AppKit.h>

#include <tray/provider.h>
#include <tray/macos/macos.h>
#include <log/log.h>

#include "../tray_internal.h"

@interface MelTrayTarget: NSObject <NSMenuDelegate>
@end

static NSMutableDictionary<NSNumber*, id>* g_trays;
static NSMutableDictionary<NSNumber*, id>* g_menus;
static NSMutableDictionary<NSNumber*, id>* g_items;
static MelTrayTarget*                      g_target;

static NSNumber* key64(u64 token) { return @(token); }

static void put_in(NSMutableDictionary<NSNumber*, id>* d, u64 token, id obj)
{
    if (obj != nil)
        d[key64(token)] = obj;
}

static id   get_in(NSMutableDictionary<NSNumber*, id>* d, u64 token) { return d[key64(token)]; }
static void drop_in(NSMutableDictionary<NSNumber*, id>* d, u64 token) { [d removeObjectForKey:key64(token)]; }

static void ensure_state(void)
{
    if (g_trays == nil)
        g_trays = [[NSMutableDictionary alloc] init];
    if (g_menus == nil)
        g_menus = [[NSMutableDictionary alloc] init];
    if (g_items == nil)
        g_items = [[NSMutableDictionary alloc] init];
    if (g_target == nil)
        g_target = [[MelTrayTarget alloc] init];
}

static NSString* nsstr(str8 s)
{
    if (s.len == 0 || s.data == NULL)
        return @"";
    return [[NSString alloc] initWithBytes:s.data length:(NSUInteger)s.len encoding:NSUTF8StringEncoding] ?: @"";
}

static NSImage* image_from(Mel_Tray_Image img)
{
    if (img.rgba != NULL && img.width > 0 && img.height > 0)
    {
        NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
                                                                        pixelsWide:(NSInteger)img.width
                                                                        pixelsHigh:(NSInteger)img.height
                                                                     bitsPerSample:8
                                                                   samplesPerPixel:4
                                                                          hasAlpha:YES
                                                                          isPlanar:NO
                                                                    colorSpaceName:NSDeviceRGBColorSpace
                                                                       bytesPerRow:(NSInteger)img.width * 4
                                                                      bitsPerPixel:32];
        if (rep == nil)
            return nil;
        memcpy(rep.bitmapData, img.rgba, (size_t)img.width * (size_t)img.height * 4);
        NSImage* nsimg = [[NSImage alloc] initWithSize:NSMakeSize(img.width, img.height)];
        [nsimg addRepresentation:rep];
        nsimg.template = img.template_mask ? YES : NO;
        return nsimg;
    }
    if (img.path.len > 0 && img.path.data != NULL)
    {
        NSImage* nsimg = [[NSImage alloc] initWithContentsOfFile:nsstr(img.path)];
        if (nsimg != nil)
            nsimg.template = img.template_mask ? YES : NO;
        return nsimg;
    }
    return nil;
}

static bool macos_supported(void* user)
{
    (void)user;
    return [NSStatusBar systemStatusBar] != nil;
}

static Mel_Tray_Status macos_create(void* user, const Mel_Tray_Lowered* lowered)
{
    (void)user;
    ensure_state();
    NSStatusItem* item = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    if (item == nil)
    {
        mel_log_error("tray", "macOS statusItemWithLength returned nil");
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_BACKEND_FAIL;
    }
    Mel_Tray_Status warn = 0;
    NSImage*        img = image_from(lowered->image);
    if (img != nil)
        item.button.image = img;
    else if (lowered->title.len > 0)
        item.button.title = nsstr(lowered->title);
    if (lowered->tooltip.len > 0)
        item.button.toolTip = nsstr(lowered->tooltip);
    item.visible = lowered->visible;

    NSMenu* menu = get_in(g_menus, lowered->menu_token);
    if (menu != nil)
    {
        menu.delegate = g_target;
        item.menu = menu;
    }

    put_in(g_trays, lowered->token, item);
    return warn ? (MEL_TRAY_WARNED | warn) : MEL_TRAY_OK;
}

static void macos_destroy(void* user, u64 token)
{
    (void)user;
    NSStatusItem* item = get_in(g_trays, token);
    if (item != nil)
        [[NSStatusBar systemStatusBar] removeStatusItem:item];
    drop_in(g_trays, token);
}

static Mel_Tray_Status macos_set_image(void* user, u64 token, Mel_Tray_Image image)
{
    (void)user;
    NSStatusItem* item = get_in(g_trays, token);
    if (item == nil)
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_BACKEND_FAIL;
    item.button.image = image_from(image);
    return MEL_TRAY_OK;
}

static Mel_Tray_Status macos_set_tooltip(void* user, u64 token, str8 tooltip)
{
    (void)user;
    NSStatusItem* item = get_in(g_trays, token);
    if (item == nil)
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_BACKEND_FAIL;
    item.button.toolTip = nsstr(tooltip);
    return MEL_TRAY_OK;
}

static Mel_Tray_Status macos_set_title(void* user, u64 token, str8 title)
{
    (void)user;
    NSStatusItem* item = get_in(g_trays, token);
    if (item == nil)
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_BACKEND_FAIL;
    item.button.title = nsstr(title);
    return MEL_TRAY_OK;
}

static Mel_Tray_Status macos_set_visible(void* user, u64 token, bool visible)
{
    (void)user;
    NSStatusItem* item = get_in(g_trays, token);
    if (item == nil)
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_BACKEND_FAIL;
    item.visible = visible;
    return MEL_TRAY_OK;
}

static Mel_Tray_Status macos_menu_create(void* user, u64 menu_token)
{
    (void)user;
    ensure_state();
    NSMenu* menu = [[NSMenu alloc] init];
    menu.autoenablesItems = NO;
    menu.delegate = g_target;
    put_in(g_menus, menu_token, menu);
    return MEL_TRAY_OK;
}

static void macos_menu_destroy(void* user, u64 menu_token)
{
    (void)user;
    drop_in(g_menus, menu_token);
}

static void apply_flags(NSMenuItem* mi, Mel_Tray_Item_Flags flags)
{
    mi.enabled = (flags & MEL_TRAY_ITEM_ENABLED) != 0;
    if ((flags & MEL_TRAY_ITEM_CHECKBOX) != 0)
        mi.state = (flags & MEL_TRAY_ITEM_CHECKED) != 0 ? NSControlStateValueOn : NSControlStateValueOff;
    else
        mi.state = NSControlStateValueOff;
}

static Mel_Tray_Status macos_item_add(void* user, const Mel_Tray_Item_Lowered* lowered)
{
    (void)user;
    NSMenu* menu = get_in(g_menus, lowered->parent_menu_token);
    if (menu == nil)
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_BACKEND_FAIL;

    NSMenuItem* mi;
    if ((lowered->flags & MEL_TRAY_ITEM_SEPARATOR) != 0)
    {
        mi = [NSMenuItem separatorItem];
    }
    else
    {
        mi = [[NSMenuItem alloc] initWithTitle:nsstr(lowered->label) action:@selector(menuItemClicked:) keyEquivalent:@""];
        mi.target = g_target;
        mi.representedObject = @(lowered->token);
        apply_flags(mi, lowered->flags);
        if (lowered->submenu_token != 0)
        {
            NSMenu* sub = get_in(g_menus, lowered->submenu_token);
            if (sub != nil)
            {
                mi.submenu = sub;
                mi.action = nil;
                mi.target = nil;
            }
        }
    }
    [menu insertItem:mi atIndex:(NSInteger)lowered->at];
    put_in(g_items, lowered->token, mi);
    return MEL_TRAY_OK;
}

static void macos_item_remove(void* user, u64 token)
{
    (void)user;
    NSMenuItem* mi = get_in(g_items, token);
    if (mi != nil && mi.menu != nil)
        [mi.menu removeItem:mi];
    drop_in(g_items, token);
}

static Mel_Tray_Status macos_item_set_label(void* user, u64 token, str8 label)
{
    (void)user;
    NSMenuItem* mi = get_in(g_items, token);
    if (mi == nil)
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_BACKEND_FAIL;
    mi.title = nsstr(label);
    return MEL_TRAY_OK;
}

static Mel_Tray_Status macos_item_set_flags(void* user, u64 token, Mel_Tray_Item_Flags flags)
{
    (void)user;
    NSMenuItem* mi = get_in(g_items, token);
    if (mi == nil)
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_BACKEND_FAIL;
    apply_flags(mi, flags);
    return MEL_TRAY_OK;
}

static void* macos_native(void* user, u64 token)
{
    (void)user;
    return (__bridge void*)get_in(g_trays, token);
}

@implementation MelTrayTarget
- (void)menuItemClicked:(id)sender
{
    NSMenuItem* mi = (NSMenuItem*)sender;
    NSNumber*   tok = mi.representedObject;
    if (tok != nil)
        mel_tray__dispatch_item_clicked(tok.unsignedLongLongValue);
}
- (void)menuWillOpen:(NSMenu*)menu
{
    (void)menu;
}
- (void)menuDidClose:(NSMenu*)menu
{
    (void)menu;
}
@end

void mel_tray__register_host_providers(void)
{
    static const Mel_Tray_Provider_Desc desc = {
        .name = "macos-nsstatusitem",
        .supported = macos_supported,
        .create = macos_create,
        .destroy = macos_destroy,
        .set_image = macos_set_image,
        .set_tooltip = macos_set_tooltip,
        .set_title = macos_set_title,
        .set_visible = macos_set_visible,
        .menu_create = macos_menu_create,
        .menu_destroy = macos_menu_destroy,
        .item_add = macos_item_add,
        .item_remove = macos_item_remove,
        .item_set_label = macos_item_set_label,
        .item_set_flags = macos_item_set_flags,
        .native = macos_native,
    };
    mel_tray_provider_register(&desc);
}

NSStatusItem* mel_tray_macos_status_item(Mel_Tray t) { return (__bridge NSStatusItem*)mel_tray_native(t); }

NSMenu* mel_tray_macos_menu(Mel_Tray t)
{
    Mel_Tray_Menu m = mel_tray_menu(t);
    return get_in(g_menus, mel_slotmap_handle_pack64(m.h));
}
