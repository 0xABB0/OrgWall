#import <Cocoa/Cocoa.h>

#include <gui.control/canvas.h>
#include <gui.platform.macos/gui.platform.macos.h>

@interface MelCanvasView : NSView
@property (nonatomic) Mel_Gui_Handle melHandle;
@end

@implementation MelCanvasView

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    CGContextRef ctx = [NSGraphicsContext currentContext].CGContext;
    mel_gui_send_message(self.melHandle, MEL_GUI_MSG_PAINT, 0, (Mel_Gui_LParam)(intptr_t)ctx);
}

- (Mel_Gui_LParam)packLocation:(NSEvent*)event
{
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    return mel_gui_pack_xy((i32)p.x, (i32)p.y);
}

- (void)mouseDown:(NSEvent*)event
{
    [self.window makeFirstResponder:self];
    mel_gui_send_message(self.melHandle, MEL_GUI_MSG_POINTER_DOWN, 1u, [self packLocation:event]);
}
- (void)mouseUp:(NSEvent*)event
{
    mel_gui_send_message(self.melHandle, MEL_GUI_MSG_POINTER_UP, 1u, [self packLocation:event]);
}
- (void)mouseDragged:(NSEvent*)event
{
    mel_gui_send_message(self.melHandle, MEL_GUI_MSG_POINTER_MOVE, 1u, [self packLocation:event]);
}
- (void)mouseMoved:(NSEvent*)event
{
    mel_gui_send_message(self.melHandle, MEL_GUI_MSG_POINTER_MOVE, 0u, [self packLocation:event]);
}

- (void)keyDown:(NSEvent*)event
{
    mel_gui_send_message(self.melHandle, MEL_GUI_MSG_KEY_DOWN, (Mel_Gui_WParam)event.keyCode, 0);
}
- (void)keyUp:(NSEvent*)event
{
    mel_gui_send_message(self.melHandle, MEL_GUI_MSG_KEY_UP, (Mel_Gui_WParam)event.keyCode, 0);
}

@end

static NSView* mel__canvas_construct(Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc)
{
    (void)desc;
    MelCanvasView* v = [[MelCanvasView alloc] initWithFrame:NSZeroRect];
    v.melHandle = h;
    return v;
}

void mel_gui_canvas_platform_register(Mel_Atom atom)
{
    mel_gui_macos_register_constructor(atom, mel__canvas_construct);
}
