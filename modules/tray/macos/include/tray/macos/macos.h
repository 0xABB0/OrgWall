#pragma once

#include <tray/tray.h>

#ifdef __OBJC__
#import <AppKit/AppKit.h>

NSStatusItem* mel_tray_macos_status_item(Mel_Tray t);
NSMenu*       mel_tray_macos_menu(Mel_Tray t);
#endif
