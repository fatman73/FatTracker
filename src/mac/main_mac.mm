// FatTracker for macOS: application, window, frame loop and input (AppKit). The screen itself is the shared
// PlayScreen drawing through Gfx/Text/Font on Core Graphics; FTView shows each finished frame as its
// layer's contents and passes keys (as Windows VK codes, src/mac/keymap_mac.cpp), mouse, wheel and dropped
// files to app_glue. --shot <file.png> ... renders offscreen and exits (src/app/shot.cpp).
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <unistd.h>
#include <string>
#include <vector>

#include "mac/app_glue.h"
#include "app/window_rect.h"
#include "mac/keymap_mac.h"
#include "platform/os.h"
#include "platform/vk.h"
#include "ui/cursor_art.h"

namespace {
NSString* appSupportDir() {
    NSArray* a = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
    NSString* base = a.count ? a[0] : NSHomeDirectory();
    return [base stringByAppendingPathComponent:@"FatTracker"];
}

// device-dependent modifier bits (IOKit/hidsystem/IOLLEvent.h NX_DEVICE*KEYMASK): which side is held
constexpr NSUInteger kLeftCtrl = 0x01, kLeftShift = 0x02, kRightShift = 0x04, kLeftCmd = 0x08, kRightCmd = 0x10,
                     kLeftAlt = 0x20, kRightAlt = 0x40, kRightCtrl = 0x2000;

// shift / ctrl / alt as the UI expects them: Command works as Ctrl (Cmd+Z, Cmd+C ...), Option is Alt
void mods(NSEventModifierFlags f, bool& shift, bool& ctrl, bool& alt) {
    shift = (f & NSEventModifierFlagShift) != 0;
    ctrl = (f & (NSEventModifierFlagControl | NSEventModifierFlagCommand)) != 0;
    alt = (f & NSEventModifierFlagOption) != 0;
}

NSString* iniPath() { return [appSupportDir() stringByAppendingPathComponent:@"FatTracker.ini"]; }

// SC68 / YMST external replays: the user's own in Application Support, else the app's resources
NSString* replayDir() {
    NSString* user = [appSupportDir() stringByAppendingPathComponent:@"Replay"];
    BOOL dir = NO;
    if ([NSFileManager.defaultManager fileExistsAtPath:user isDirectory:&dir] && dir) return user;
    NSString* res = NSBundle.mainBundle.resourcePath ?: @".";
    return [res stringByAppendingPathComponent:@"Replay"];
}

// Pure mode drive sounds (floppy_motor/step/insert.wav, supplied by the user): Application Support first,
// then the app's resources
std::vector<std::string> soundDirs() {
    NSString* res = NSBundle.mainBundle.resourcePath ?: @".";
    return {std::string([appSupportDir() stringByAppendingPathComponent:@"sounds"].fileSystemRepresentation) + "/",
            std::string([res stringByAppendingPathComponent:@"sounds"].fileSystemRepresentation) + "/"};
}

// The Amiga pointer (src/ui/cursor_art.cpp) at 32 points, with 1x and 2x pixels (main.cpp builds it per DPI).
// The premultiplied 0xAARRGGBB words go through a CGImage (32-bit little-endian, alpha first: the layout gfx_mac
// draws with); an NSBitmapImageRep filled directly in that layout gave an unusable cursor image.
NSCursor* amigaCursor(bool busy) {
    NSImage* img = [[NSImage alloc] initWithSize:NSMakeSize(32, 32)];
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    for (int scale = 1; scale <= 2; ++scale) {
        const int px = 32 * scale;
        std::vector<uint32_t> argb((size_t)px * px);
        renderAmigaCursor(argb.data(), px, busy);
        CGContextRef ctx = CGBitmapContextCreate(argb.data(), px, px, 8, px * 4, cs,
                                                 (uint32_t)kCGBitmapByteOrder32Little |
                                                     (uint32_t)kCGImageAlphaPremultipliedFirst);
        if (!ctx) continue;
        CGImageRef cg = CGBitmapContextCreateImage(ctx);
        CGContextRelease(ctx);
        if (!cg) continue;
        NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithCGImage:cg];
        CGImageRelease(cg);
        rep.size = NSMakeSize(32, 32);
        [img addRepresentation:rep];
    }
    CGColorSpaceRelease(cs);
    int hx = 0, hy = 0;
    amigaCursorHotspot(32, hx, hy);
    return [[NSCursor alloc] initWithImage:img hotSpot:NSMakePoint(hx, hy)];
}

NSCursor* g_arrowCursor = nil;
NSCursor* g_busyCursor = nil;
NSCursor* currentCursor() {
    if (!g_arrowCursor) {
        g_arrowCursor = amigaCursor(false);
        g_busyCursor = amigaCursor(true);
    }
    return appBusy() ? g_busyCursor : g_arrowCursor;
}
}  // namespace

// ---------------------------------------------------------------- view

@interface FTView : NSView
@end

@implementation FTView {
    NSTimer* _frameTimer;
    NSTimer* _audioTimer;
    NSTimer* _clockTimer;
    NSTrackingArea* _tracking;
    CGFloat _wheelAcc;       // trackpad scrolling, in points, until a notch is full
    NSUInteger _prevFlags;   // modifier flags of the last flagsChanged (which modifier went down or up)
    BOOL _ctrlClick;         // the left button went down with Control: a right click (no mouse-up for it)
    CALayer* _part;          // a logo-only frame: just the redrawn pixels, over the last full frame
}

- (instancetype)initWithFrame:(NSRect)r {
    if ((self = [super initWithFrame:r])) {
        self.wantsLayer = YES;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawOnSetNeedsDisplay;
        self.layer.contentsGravity = kCAGravityResize;
        self.layer.magnificationFilter = kCAFilterNearest;
        _part = [CALayer layer];
        _part.hidden = YES;
        _part.magnificationFilter = kCAFilterNearest;
        _part.actions = @{@"contents" : [NSNull null], @"position" : [NSNull null], @"bounds" : [NSNull null],
                          @"hidden" : [NSNull null]};
        [self.layer addSublayer:_part];
        __weak FTView* weakSelf = self;
        // frame pacing: ~60 Hz; a frame is drawn only when something changed or the screen animates
        _frameTimer = [NSTimer timerWithTimeInterval:1.0 / 60.0
                                             repeats:YES
                                               block:^(NSTimer*) { [weakSelf tick]; }];
        _audioTimer = [NSTimer timerWithTimeInterval:2.0
                                             repeats:YES
                                               block:^(NSTimer*) { appPollAudio(); }];
        _clockTimer = [NSTimer timerWithTimeInterval:1.0 repeats:YES block:^(NSTimer*) { appClockTick(); }];
        for (NSTimer* t in @[ _frameTimer, _audioTimer, _clockTimer ])
            [[NSRunLoop mainRunLoop] addTimer:t forMode:NSRunLoopCommonModes];
        [self registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
    }
    return self;
}

- (BOOL)isFlipped {
    return YES;
}
- (BOOL)wantsUpdateLayer {
    return YES;
}
- (BOOL)acceptsFirstResponder {
    return YES;
}
- (BOOL)acceptsFirstMouse:(NSEvent*)e {
    return YES;  // a click on the inactive window also acts (like Windows)
}

// A new size: the frame is redrawn at once at that size and shown, so a live resize scales the screen smoothly
// step by step (like WM_SIZE on Windows) instead of showing the new, still empty bitmap until the next tick.
- (void)syncSize {
    const CGFloat s = self.window ? self.window.backingScaleFactor : 2.0;
    const NSSize b = self.bounds.size;
    const uint32_t w = (uint32_t)std::lround(b.width * s), h = (uint32_t)std::lround(b.height * s);
    self.layer.contentsScale = s;
    _part.contentsScale = s;
    if (!w || !h) return;
    if (self.window.colorSpace.CGColorSpace) appSetColorSpace(self.window.colorSpace.CGColorSpace);
    appResize(w, h);
    if (self.window && appDrawFrame()) [self updateLayer];
}

- (void)setFrameSize:(NSSize)size {
    [super setFrameSize:size];
    [self syncSize];
}

- (void)viewDidChangeBackingProperties {
    [super viewDidChangeBackingProperties];
    [self syncSize];
}

- (void)tick {
    if (!appFrameDue()) return;
    if (!appDrawFrame()) return;
    int x, y, w, h;
    if (!appFramePartial(&x, &y, &w, &h)) {
        [self setNeedsDisplay:YES];  // a full frame: updateLayer
        return;
    }
    // only the logo moved: hand the window server that small part, not the whole frame
    CGImageRef img = appCopyPartialFrame();
    if (!img) return;
    const CGFloat s = self.layer.contentsScale > 0 ? self.layer.contentsScale : 1;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    _part.frame = CGRectMake(x / s, y / s, w / s, h / s);  // the view is flipped: top-left origin
    _part.contents = (__bridge id)img;
    _part.hidden = NO;
    [CATransaction commit];
    CGImageRelease(img);
}

// the Amiga pointer over the view (busy while the simulated floppy reads)
- (void)resetCursorRects {
    [self addCursorRect:self.bounds cursor:currentCursor()];
}
- (void)cursorUpdate:(NSEvent*)e {
    [currentCursor() set];
}

// ---- mouse: points in the flipped view -> device pixels (the frame is drawn at the backing scale)

- (NSPoint)px:(NSEvent*)e {
    const NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    const CGFloat s = self.window ? self.window.backingScaleFactor : 2.0;
    return NSMakePoint(p.x * s, p.y * s);
}

- (void)mouseDown:(NSEvent*)e {
    const NSPoint p = [self px:e];
    _ctrlClick = (e.modifierFlags & NSEventModifierFlagControl) != 0;  // Control+click = right click (Mac)
    appMouseDown(p.x, p.y, _ctrlClick);
}
- (void)mouseUp:(NSEvent*)e {
    const NSPoint p = [self px:e];
    if (_ctrlClick) _ctrlClick = NO;  // Windows sends no button-up for the right button either
    else appMouseUp(p.x, p.y);
}
- (void)mouseDragged:(NSEvent*)e {
    const NSPoint p = [self px:e];
    appMouseMove(p.x, p.y, !_ctrlClick);
}
- (void)mouseMoved:(NSEvent*)e {
    const NSPoint p = [self px:e];
    appMouseMove(p.x, p.y, false);
}
- (void)rightMouseDown:(NSEvent*)e {
    const NSPoint p = [self px:e];
    appMouseDown(p.x, p.y, true);
}
- (void)rightMouseDragged:(NSEvent*)e {
    const NSPoint p = [self px:e];
    appMouseMove(p.x, p.y, false);
}

- (void)updateTrackingAreas {
    if (_tracking) [self removeTrackingArea:_tracking];
    _tracking = [[NSTrackingArea alloc]
        initWithRect:NSZeroRect
             options:NSTrackingMouseMoved | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect | NSTrackingCursorUpdate
               owner:self
            userInfo:nil];
    [self addTrackingArea:_tracking];
    [super updateTrackingAreas];
}

// Wheel: Windows gives 120 per notch, positive = away from the user. A mouse wheel here gives lines; the
// trackpad gives points, collected until a notch is full. The sign follows the system's scroll direction.
- (void)scrollWheel:(NSEvent*)e {
    const NSPoint p = [self px:e];
    const CGFloat d = e.scrollingDeltaY;
    if (!e.hasPreciseScrollingDeltas) {
        if (d == 0) return;
        const long n = std::max(1L, std::lround(std::fabs(d)));
        appWheel(p.x, p.y, (int)(d > 0 ? n : -n) * 120);
        return;
    }
    if (e.phase == NSEventPhaseBegan) _wheelAcc = 0;
    _wheelAcc += d;
    constexpr CGFloat kNotch = 12;  // points of trackpad travel per notch
    int n = 0;
    for (; _wheelAcc >= kNotch; _wheelAcc -= kNotch) ++n;
    for (; _wheelAcc <= -kNotch; _wheelAcc += kNotch) --n;
    if (n) appWheel(p.x, p.y, n * 120);
}

// ---- keyboard: positional key codes -> VK; characters for name fields and DISK OP. go to onChar

- (void)keyDown:(NSEvent*)e {
    const NSEventModifierFlags f = e.modifierFlags;
    bool shift, ctrl, alt;
    mods(f, shift, ctrl, alt);
    const unsigned vk = vkFromMac(e.keyCode);
    if (vk == VK_RETURN && alt && !ctrl) {  // Alt+Enter: full screen, like Windows
        if (!e.isARepeat) [self.window toggleFullScreen:nil];
        return;
    }
    unsigned key = vk;
    // Mac stand-ins for keys a MacBook lacks: Option+Delete = Insert (Shift: all tracks), Ctrl/Cmd + 1..9 of
    // the digit row = Ctrl + keypad 1..9 (mute a track of the view; the digit row does nothing with Ctrl)
    if (vk == VK_BACK && alt && !ctrl) {
        key = VK_INSERT;
        alt = false;
    } else if (ctrl && !alt && vk >= '1' && vk <= '9') {
        key = VK_NUMPAD1 + (vk - '1');
    }
    if (key && appKey(key, shift, ctrl, alt, false, e.isARepeat)) return;  // it only ended a floppy load
    if (f & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) return;  // Ctrl+key types nothing
    if (key == VK_INSERT) return;
    NSString* chars = e.characters;
    for (NSUInteger i = 0; i < chars.length; ++i) {
        const unichar c = [chars characterAtIndex:i];
        if (c < 32 || c == 127 || (c >= 0xF700 && c <= 0xF8FF)) continue;  // controls, arrows, F-keys
        if (alt && c < 128) continue;  // Option+key is a shortcut (Windows drops Alt characters); letters like ą still type
        appChar(c);
    }
}

- (void)keyUp:(NSEvent*)e {
    unsigned vk = vkFromMac(e.keyCode);
    if (vk == VK_BACK && (e.modifierFlags & NSEventModifierFlagOption)) vk = VK_INSERT;
    if (vk) appKeyUp(vk);
}

// A modifier alone sends no keyDown: compare the device bits to see which one went down or up. Right
// Option = PLAY song, right Control = PLAY pattern (extended, like the right keys on Windows); right Command
// also plays the pattern (a MacBook has no right Control).
- (void)flagsChanged:(NSEvent*)e {
    const NSUInteger now = e.modifierFlags, was = _prevFlags;
    _prevFlags = now;
    NSUInteger mask = 0;
    unsigned vk = 0;
    bool ext = false;
    switch (e.keyCode) {
        case 0x38: mask = kLeftShift; vk = VK_SHIFT; break;
        case 0x3C: mask = kRightShift; vk = VK_SHIFT; break;
        case 0x3B: mask = kLeftCtrl; vk = VK_CONTROL; break;
        case 0x3E: mask = kRightCtrl; vk = VK_CONTROL; ext = true; break;
        case 0x3A: mask = kLeftAlt; vk = VK_MENU; break;
        case 0x3D: mask = kRightAlt; vk = VK_MENU; ext = true; break;
        case 0x37: mask = kLeftCmd; vk = VK_CONTROL; break;  // Command works as Ctrl
        case 0x36: mask = kRightCmd; vk = VK_CONTROL; ext = true; break;
        default: return;
    }
    bool shift, ctrl, alt;
    mods(now, shift, ctrl, alt);
    if ((now & mask) && !(was & mask)) appKey(vk, shift, ctrl, alt, ext, false);
    else if (!(now & mask) && (was & mask)) appKeyUp(vk);
}

// ---- files dropped from the Finder: a module or a sample (like WM_DROPFILES)

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)info {
    return NSDragOperationCopy;
}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)info {
    NSArray<NSURL*>* urls = [info.draggingPasteboard readObjectsForClasses:@[ NSURL.class ]
                                                                    options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
    if (!urls.count) return NO;
    appDropFile(urls.firstObject.fileSystemRepresentation);
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    return YES;
}

- (void)updateLayer {
    CGImageRef img = appCopyFrame();
    if (!img) return;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    self.layer.contents = (__bridge id)img;
    _part.hidden = YES;  // the full frame has the logo too
    _part.contents = nil;
    [CATransaction commit];
    CGImageRelease(img);
}

// --input-test: after a full frame and then a logo-only frame (the small layer), what the layers show must be
// the frame's pixels. Returns the differing pixels (-1: no logo-only frame happened).
- (long)checkLayerTree {
    appSetDemoTime(1.0);
    appSetDirty();
    [self tick];
    [self updateLayer];  // the full frame on the layer now (demo at 1 s)
    appSetDemoTime(2.0);
    [self tick];         // a logo-only frame (demo at 2 s), if only the logo moves
    appSetDemoTime(-1);
    if (_part.hidden) return -1;
    CGImageRef want = appCopyFrame();
    const size_t w = CGImageGetWidth(want), h = CGImageGetHeight(want);
    CGColorSpaceRef cs = CGImageGetColorSpace(want);
    CGContextRef c = CGBitmapContextCreate(nullptr, w, h, 8, w * 4, cs,
                                           (uint32_t)kCGBitmapByteOrder32Little | (uint32_t)kCGImageAlphaPremultipliedFirst);
    const CGFloat s = self.layer.contentsScale;
    CGContextScaleCTM(c, s, s);
    if (self.layer.geometryFlipped) {  // layer space is y-down: render it upright in the bitmap
        CGContextTranslateCTM(c, 0, self.bounds.size.height);
        CGContextScaleCTM(c, 1, -1);
    }
    [self.layer renderInContext:c];
    CGImageRef got = CGBitmapContextCreateImage(c);
    CGContextRelease(c);
    CFDataRef a = CGDataProviderCopyData(CGImageGetDataProvider(want));
    CFDataRef b = CGDataProviderCopyData(CGImageGetDataProvider(got));
    const uint8_t* pa = CFDataGetBytePtr(a);
    const uint8_t* pb = CFDataGetBytePtr(b);
    const size_t ra = CGImageGetBytesPerRow(want), rb = CGImageGetBytesPerRow(got);
    long diff = 0;
    for (size_t y = 0; y < h; ++y)
        for (size_t x = 0; x < w; ++x) {
            const uint8_t* qa = pa + y * ra + x * 4;
            const uint8_t* qb = pb + y * rb + x * 4;
            diff += std::abs(qa[0] - qb[0]) > 2 || std::abs(qa[1] - qb[1]) > 2 || std::abs(qa[2] - qb[2]) > 2;
        }
    CFRelease(a);
    CFRelease(b);
    CGImageRelease(want);
    CGImageRelease(got);
    return diff;
}

- (void)stop {
    [_frameTimer invalidate];
    [_audioTimer invalidate];
    [_clockTimer invalidate];
}
@end

// ---------------------------------------------------------------- application

// The save panel's file type (like the Windows dialog's filter list): XM / MOD / WAV, keeps the name's extension
// in step with the choice.
@interface FTSaveType : NSObject
@property(weak) NSSavePanel* panel;
@property(strong) NSPopUpButton* popup;
@end

@implementation FTSaveType
- (void)changed:(id)sender {
    static NSString* const ext[3] = {@"xm", @"mod", @"wav"};
    NSString* name = self.panel.nameFieldStringValue;
    self.panel.nameFieldStringValue = [name.stringByDeletingPathExtension stringByAppendingPathExtension:ext[self.popup.indexOfSelectedItem]];
}
@end

@interface FTAppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property(copy) NSString* inputTestDir;  // --input-test <dir>: scripted events, PNGs, quit (no audio)
@property(strong) NSWindow* window;
@property(strong) FTView* view;
@property(strong) NSMutableArray<NSURL*>* pendingOpen;
@property(strong) NSTimer* floppyTimer;
@property(strong) FTSaveType* saveType;
@property BOOL closeConfirmed;  // the unsaved-changes question was answered: close / quit for real
@property NSRect normalFrame;   // the window's frame when neither zoomed nor full screen (saved in [Window])
@property BOOL wasZoomed;       // zoomed (Windows: maximized) when last seen
@property BOOL inFullScreen;
@end

@implementation FTAppDelegate

- (void)buildMenu {
    NSMenu* bar = [NSMenu new];
    NSMenuItem* appItem = [NSMenuItem new];
    [bar addItem:appItem];
    NSMenu* appMenu = [NSMenu new];
    [appMenu addItemWithTitle:@"Hide FatTracker" action:@selector(hide:) keyEquivalent:@"h"];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"Quit FatTracker" action:@selector(terminate:) keyEquivalent:@"q"];
    appItem.submenu = appMenu;
    NSMenuItem* fileItem = [NSMenuItem new];
    [bar addItem:fileItem];
    NSMenu* fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
    [fileMenu addItemWithTitle:@"Open\u2026" action:@selector(openDocument:) keyEquivalent:@"o"];
    fileItem.submenu = fileMenu;
    NSMenuItem* winItem = [NSMenuItem new];
    [bar addItem:winItem];
    NSMenu* winMenu = [[NSMenu alloc] initWithTitle:@"Window"];
    [winMenu addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
    NSMenuItem* fs = [winMenu addItemWithTitle:@"Enter Full Screen" action:@selector(toggleFullScreen:) keyEquivalent:@"f"];
    fs.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagControl;
    winItem.submenu = winMenu;
    NSApp.mainMenu = bar;
    NSApp.windowsMenu = winMenu;
}

// AppKit services for the shared app logic (AppShell, through app_glue's MacHost)
- (void)installHost {
    __weak FTAppDelegate* weakSelf = self;
    MacHost h;
    h.setTitle = [weakSelf](const char* name, bool dirty) {
        FTAppDelegate* me = weakSelf;
        me.window.title = [NSString stringWithFormat:@"FatTracker \u2014 %@", [NSString stringWithUTF8String:name] ?: @"?"];
        me.window.documentEdited = dirty;
    };
    h.setBusy = [weakSelf](bool) {
        FTAppDelegate* me = weakSelf;
        [me.window invalidateCursorRectsForView:me.view];
        [currentCursor() set];
    };
    h.startFloppyTimer = [weakSelf](double seconds) {
        FTAppDelegate* me = weakSelf;
        [me.floppyTimer invalidate];
        me.floppyTimer = [NSTimer scheduledTimerWithTimeInterval:seconds
                                                         repeats:NO
                                                           block:^(NSTimer*) {
                                                               weakSelf.floppyTimer = nil;
                                                               appFloppyTimer();
                                                           }];
    };
    h.stopFloppyTimer = [weakSelf]() {
        [weakSelf.floppyTimer invalidate];
        weakSelf.floppyTimer = nil;
    };
    h.openDialog = [weakSelf](const char* startDir, std::function<void(const char*)> done) {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.canChooseDirectories = NO;
        panel.allowsMultipleSelection = NO;
        panel.message = @"Open a module";
        if (*startDir) panel.directoryURL = [NSURL fileURLWithPath:@(startDir) isDirectory:YES];
        [panel beginSheetModalForWindow:weakSelf.window
                      completionHandler:^(NSModalResponse r) {
                          if (r == NSModalResponseOK && panel.URL) done(panel.URL.fileSystemRepresentation);
                      }];
    };
    h.saveDialog = [weakSelf](const char* dir, const char* name, int filter, std::function<void(const char*, int)> done) {
        FTAppDelegate* me = weakSelf;
        NSSavePanel* panel = [NSSavePanel savePanel];
        panel.allowsOtherFileTypes = YES;
        panel.canCreateDirectories = YES;
        if (*dir) panel.directoryURL = [NSURL fileURLWithPath:@(dir) isDirectory:YES];
        panel.nameFieldStringValue = @(name);
        NSPopUpButton* popup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 260, 26) pullsDown:NO];
        [popup addItemsWithTitles:@[ @"FastTracker 2 module (.xm)", @"ProTracker module (.mod)", @"WAV audio (.wav)" ]];
        [popup selectItemAtIndex:std::clamp(filter, 1, 3) - 1];
        me.saveType = [FTSaveType new];
        me.saveType.panel = panel;
        me.saveType.popup = popup;
        popup.target = me.saveType;
        popup.action = @selector(changed:);
        NSTextField* label = [NSTextField labelWithString:@"Format:"];
        NSStackView* row = [NSStackView stackViewWithViews:@[ label, popup ]];
        row.edgeInsets = NSEdgeInsetsMake(8, 8, 8, 8);
        panel.accessoryView = row;
        [me.saveType changed:nil];
        [panel beginSheetModalForWindow:me.window
                      completionHandler:^(NSModalResponse r) {
                          const int chosen = (int)popup.indexOfSelectedItem + 1;
                          weakSelf.saveType = nil;
                          if (r == NSModalResponseOK && panel.URL) done(panel.URL.fileSystemRepresentation, chosen);
                      }];
    };
    appSetHost(h);
}

// ---- the window comes back where it was (main.cpp: [Window] X, Y, Width, Height, Maximized, Fullscreen).
// The rectangle is the whole window frame in points, top-left origin on the primary screen (y down, like the
// Windows screen pixels), so the shared fitWindowRect works on it.

static CGFloat primaryTop() { return NSScreen.screens.count ? NSMaxY(NSScreen.screens[0].frame) : 0; }
static WinRect toTopLeft(NSRect r) {
    const CGFloat top = primaryTop() - NSMaxY(r);
    return {(int)std::lround(r.origin.x), (int)std::lround(top), (int)std::lround(NSMaxX(r)),
            (int)std::lround(top + r.size.height)};
}
static NSRect fromTopLeft(const WinRect& w) {
    return NSMakeRect(w.left, primaryTop() - w.bottom, w.right - w.left, w.bottom - w.top);
}

- (void)rememberPlacement {
    if (self.inFullScreen || self.window.miniaturized) return;
    self.wasZoomed = self.window.zoomed;
    if (!self.wasZoomed) self.normalFrame = self.window.frame;
}
- (void)windowDidMove:(NSNotification*)n {
    [self rememberPlacement];
}
// another display (or its color profile changed): the frames follow its color space
- (void)windowDidChangeScreen:(NSNotification*)n {
    [self.view syncSize];
}
- (void)windowDidChangeScreenProfile:(NSNotification*)n {
    [self.view syncSize];
}
- (void)windowDidResize:(NSNotification*)n {
    [self rememberPlacement];
}
- (void)windowWillEnterFullScreen:(NSNotification*)n {
    [self rememberPlacement];
    self.inFullScreen = YES;
}
- (void)windowDidExitFullScreen:(NSNotification*)n {
    self.inFullScreen = NO;
    [self rememberPlacement];
}

- (void)saveWindowPlacement:(NSString*)iniFile {
    const NSRect f = self.normalFrame;
    if (f.size.width <= 0 || f.size.height <= 0) return;
    const std::wstring ini = std::filesystem::path(iniFile.fileSystemRepresentation).wstring();
    const WinRect w = toTopLeft(f);
    const wchar_t* s = L"Window";
    osIniWrite(ini.c_str(), s, L"X", std::to_wstring(w.left));
    osIniWrite(ini.c_str(), s, L"Y", std::to_wstring(w.top));
    osIniWrite(ini.c_str(), s, L"Width", std::to_wstring(w.right - w.left));
    osIniWrite(ini.c_str(), s, L"Height", std::to_wstring(w.bottom - w.top));
    osIniWrite(ini.c_str(), s, L"Maximized", self.wasZoomed ? L"1" : L"0");
    osIniWrite(ini.c_str(), s, L"Fullscreen", self.inFullScreen ? L"1" : L"0");
}

// The saved frame, whole on the screen it overlaps most (else the nearest one); NO when nothing is saved.
- (BOOL)savedFrame:(NSRect*)out ini:(NSString*)iniFile {
    const std::wstring ini = std::filesystem::path(iniFile.fileSystemRepresentation).wstring();
    const int w = osIniInt(ini.c_str(), L"Window", L"Width", 0), h = osIniInt(ini.c_str(), L"Window", L"Height", 0);
    if (w <= 0 || h <= 0 || !NSScreen.screens.count) return NO;
    const int x = osIniInt(ini.c_str(), L"Window", L"X", 0), y = osIniInt(ini.c_str(), L"Window", L"Y", 0);
    const NSRect saved = fromTopLeft({x, y, x + w, y + h});
    NSScreen* best = nil;
    CGFloat bestArea = -1, bestDist = 1e18;
    for (NSScreen* scr in NSScreen.screens) {
        const NSRect in = NSIntersectionRect(saved, scr.visibleFrame);
        const CGFloat area = in.size.width * in.size.height;
        const CGFloat dx = NSMidX(saved) - NSMidX(scr.visibleFrame), dy = NSMidY(saved) - NSMidY(scr.visibleFrame);
        const CGFloat dist = dx * dx + dy * dy;
        if (area > bestArea || (area == bestArea && dist < bestDist)) best = scr, bestArea = area, bestDist = dist;
    }
    *out = fromTopLeft(fitWindowRect(toTopLeft(saved), toTopLeft(best.visibleFrame), 640, 400));
    return YES;
}

- (void)applicationDidFinishLaunching:(NSNotification*)n {
    [self buildMenu];
    [self installHost];
    NSArray<NSString*>* args = NSProcessInfo.processInfo.arguments;
    if (args.count >= 3 && [args[1] isEqualToString:@"--input-test"]) {
        self.inputTestDir = args[2];
        chdir(self.inputTestDir.fileSystemRepresentation);  // DISK OP. browses (and saves into) the test folder
    }
    // the input test runs without the user's settings file and silently
    NSString* ini = self.inputTestDir ? [self.inputTestDir stringByAppendingPathComponent:@"input-test.ini"] : iniPath();
    NSScreen* scr = NSScreen.mainScreen;
    const NSRect vis = scr ? scr.visibleFrame : NSMakeRect(0, 0, 1600, 900);
    const CGFloat px = scr ? scr.frame.size.width * scr.backingScaleFactor : 1600;
    NSSize want = px >= 2560 ? NSMakeSize(1920, 1080) : NSMakeSize(1600, 900);
    // the whole window inside the visible area (menu bar and Dock must not cover it)
    NSRect content = NSMakeRect(0, 0, want.width, want.height);
    NSRect frame = [NSWindow frameRectForContentRect:content
                                           styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                     NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable];
    const CGFloat k = std::min({1.0, vis.size.width / frame.size.width, vis.size.height / frame.size.height});
    content.size = NSMakeSize(std::floor(want.width * k), std::floor(want.height * k));
    self.window = [[NSWindow alloc]
        initWithContentRect:content
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable |
                            NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    self.window.title = @"FatTracker";
    self.window.delegate = self;
    self.window.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;
    self.window.contentMinSize = NSMakeSize(640, 360);
    self.view = [[FTView alloc] initWithFrame:NSMakeRect(0, 0, content.size.width, content.size.height)];
    self.window.contentView = self.view;
    NSRect savedFrame;
    const BOOL haveSaved = [self savedFrame:&savedFrame ini:ini];
    if (haveSaved) [self.window setFrame:savedFrame display:NO];  // where it was last time
    else [self.window center];
    self.normalFrame = self.window.frame;
    [self.view syncSize];
    const std::wstring iniW = std::filesystem::path(ini.fileSystemRepresentation).wstring();
    const bool startZoomed = haveSaved && osIniInt(iniW.c_str(), L"Window", L"Maximized", 0) != 0;
    const bool startFullScreen = haveSaved && osIniInt(iniW.c_str(), L"Window", L"Fullscreen", 0) != 0;
    appWindowReady(ini.fileSystemRepresentation, replayDir().fileSystemRepresentation, soundDirs(), !self.inputTestDir);
    // FatTracker <module> from the command line, or files opened from the Finder before launch finished
    if (args.count >= 2 && ![args[1] hasPrefix:@"-"]) appOpenFile(args[1].fileSystemRepresentation, true);
    for (NSURL* u in self.pendingOpen) appOpenFile(u.fileSystemRepresentation, true);
    [self.pendingOpen removeAllObjects];

    [self.window makeKeyAndOrderFront:nil];
    [self.window makeFirstResponder:self.view];
    [NSApp activateIgnoringOtherApps:YES];
    if (startZoomed && !self.window.zoomed) {
        [self.window zoom:nil];
        self.wasZoomed = YES;
    }
    if (startFullScreen) [self.window toggleFullScreen:nil];
    appSetDirty();
    if (self.inputTestDir) [self runInputTest];
}

// ---- --input-test <dir>: real NSEvents through [NSApp sendEvent:] (menus, responder chain, FTView), so the
// key table, modifier handling and mouse coordinates are checked without anyone at the keyboard.

- (void)key:(uint16_t)kc chars:(NSString*)ch flags:(NSEventModifierFlags)f {
    for (NSEventType t : {NSEventTypeKeyDown, NSEventTypeKeyUp}) {
        NSEvent* e = [NSEvent keyEventWithType:t
                                      location:NSZeroPoint
                                 modifierFlags:f
                                     timestamp:NSProcessInfo.processInfo.systemUptime
                                  windowNumber:self.window.windowNumber
                                       context:nil
                                    characters:ch
                   charactersIgnoringModifiers:ch
                                     isARepeat:NO
                                       keyCode:kc];
        [NSApp sendEvent:e];
    }
}

- (void)modifier:(uint16_t)kc flags:(NSEventModifierFlags)f {
    NSEvent* e = [NSEvent keyEventWithType:NSEventTypeFlagsChanged
                                  location:NSZeroPoint
                             modifierFlags:f
                                 timestamp:NSProcessInfo.processInfo.systemUptime
                              windowNumber:self.window.windowNumber
                                   context:nil
                                characters:@""
               charactersIgnoringModifiers:@""
                                 isARepeat:NO
                                   keyCode:kc];
    [NSApp sendEvent:e];
}

// a click at a point of the 1920x1080 logical layout
- (void)clickLogical:(NSPoint)l right:(BOOL)right flags:(NSEventModifierFlags)f {
    const CGFloat s = self.window.backingScaleFactor, k = appScale() / s;
    const NSPoint inView = NSMakePoint(l.x * k, l.y * k);
    const NSPoint inWindow = [self.view convertPoint:inView toView:nil];
    const NSEventType down = right ? NSEventTypeRightMouseDown : NSEventTypeLeftMouseDown;
    const NSEventType up = right ? NSEventTypeRightMouseUp : NSEventTypeLeftMouseUp;
    for (NSEventType t : {down, up}) {
        NSEvent* e = [NSEvent mouseEventWithType:t
                                        location:inWindow
                                   modifierFlags:f
                                       timestamp:NSProcessInfo.processInfo.systemUptime
                                    windowNumber:self.window.windowNumber
                                         context:nil
                                     eventNumber:0
                                      clickCount:1
                                        pressure:t == up ? 0 : 1];
        [NSApp sendEvent:e];
    }
}

- (void)shot:(NSString*)name {
    NSString* path = [self.inputTestDir stringByAppendingPathComponent:name];
    printf("%s %s\n", appSavePng(path.fileSystemRepresentation) ? "saved" : "FAILED", path.UTF8String);
}

- (void)runInputTest {
    setvbuf(stdout, nullptr, _IOLBF, 0);  // lines reach a log file as they are printed
    const NSEventModifierFlags cmd = NSEventModifierFlagCommand, ctl = NSEventModifierFlagControl;
    {
        const WinRect w = toTopLeft(self.window.frame);
        printf("window at %d,%d %dx%d (zoomed %d, full screen %d)\n", w.left, w.top, w.right - w.left, w.bottom - w.top,
               (int)self.window.zoomed, (int)self.inFullScreen);
    }
    NSArray* steps = @[
        ^{  // edit mode, three piano keys (Z S X = C D C#... of the base octave), Cmd+Z undoes the last one
            [self key:0x31 chars:@" " flags:0];
            [self key:0x06 chars:@"z" flags:0];
            [self key:0x01 chars:@"s" flags:0];
            [self key:0x07 chars:@"x" flags:0];
            [self key:0x06 chars:@"z" flags:cmd];
        },
        ^{ [self shot:@"1_keys.png"]; },
        ^{  // the Amiga pointer the view sets (its 2x pixels)
            NSImage* cur = currentCursor().image;
            NSBitmapImageRep* best = nil;
            for (NSImageRep* r in cur.representations)
                if ([r isKindOfClass:NSBitmapImageRep.class] && (!best || r.pixelsWide > best.pixelsWide))
                    best = (NSBitmapImageRep*)r;
            NSString* path = [self.inputTestDir stringByAppendingPathComponent:@"cursor.png"];
            const BOOL ok = [[best representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:path
                                                                                                     atomically:YES];
            printf("%s %s (%ldx%ld px, hotspot %.0f,%.0f)\n", ok ? "saved" : "FAILED", path.UTF8String,
                   (long)best.pixelsWide, (long)best.pixelsHigh, currentCursor().hotSpot.x, currentCursor().hotSpot.y);
        },
        ^{  // click DISK OP., type a name ("ab" is added to the file name field), Shift+letter
            [self clickLogical:NSMakePoint(884, 150) right:NO flags:0];
            [self key:0x00 chars:@"a" flags:0];
            [self key:0x0B chars:@"B" flags:NSEventModifierFlagShift];
        },
        ^{ [self shot:@"2_diskop.png"]; },
        ^{  // Esc closes the panel; Control+click on TRACK 02's header = right click = solo
            [self key:0x35 chars:@"\x1b" flags:0];
            [self clickLogical:NSMakePoint(356, 478) right:NO flags:ctl];
        },
        ^{ [self shot:@"3_solo.png"]; },
        ^{  // right click on TRACK 02 again (un-solo), then right Option alone = PLAY song
            [self clickLogical:NSMakePoint(356, 478) right:YES flags:0];
            [self modifier:0x3D flags:NSEventModifierFlagOption | 0x40];
            [self modifier:0x3D flags:0];
        },
        ^{ [self shot:@"4_right_option_play.png"]; },
        ^{  // Space stops; F11 cycles the view (8 -> 16 tracks); Shift+F1 is not a VK combo, plain F2 = octave up
            [self key:0x31 chars:@" " flags:0];
            [self key:0x67 chars:@"\uF70E" flags:NSEventModifierFlagFunction];
            [self key:0x78 chars:@"\uF705" flags:NSEventModifierFlagFunction];
        },
        ^{ [self shot:@"5_f11_f2.png"]; },
        ^{  // Mac stand-ins: Cmd+2 mutes track 2 of the view, Option+Delete inserts a row at the cursor (row 0
            // after the stop), right Command alone = PLAY pattern
            [self key:0x13 chars:@"2" flags:cmd];
            [self key:0x33 chars:@"\x7f" flags:NSEventModifierFlagOption];
            [self modifier:0x36 flags:cmd | 0x10];
            [self modifier:0x36 flags:0];
        },
        ^{ [self shot:@"6_mac_keys.png"]; },
        ^{ [self clickLogical:NSMakePoint(1092, 208) right:NO flags:0]; },  // HELP
        ^{ [self shot:@"7_help.png"]; },
        ^{  // M6: DISK OP. SAVE XM writes <name>.xm into the folder being browsed (the test folder)
            [self key:0x35 chars:@"\x1b" flags:0];
            [self clickLogical:NSMakePoint(884, 150) right:NO flags:0];
            [self clickLogical:NSMakePoint(279, 162) right:NO flags:0];
        },
        ^{ [self shot:@"8_saved_xm.png"]; },
        ^{  // a new note makes the song dirty; loading the saved file then asks SAVE / DISCARD / CANCEL
            [self key:0x35 chars:@"\x1b" flags:0];
            [self key:0x0C chars:@"q" flags:0];
            NSArray* files = [NSFileManager.defaultManager contentsOfDirectoryAtPath:self.inputTestDir error:nil];
            for (NSString* f in files)
                if ([f.pathExtension isEqualToString:@"xm"])
                    appDropFile([self.inputTestDir stringByAppendingPathComponent:f].fileSystemRepresentation);
        },
        ^{ [self shot:@"9_unsaved_question.png"]; },
        ^{  // DISCARD (Return = the first button would be SAVE; click DISCARD instead): the file loads
            [self key:0x35 chars:@"\x1b" flags:0];
        },
        ^{ [self shot:@"10_cancelled.png"]; },
        ^{  // live resize: right after each new size the frame shown must have that size and not be empty
            const NSRect start = self.window.frame;
            for (NSSize sz : {NSMakeSize(900, 520), NSMakeSize(1240, 700), NSMakeSize(700, 400)}) {
                NSRect f = start;
                f.size = [self.window frameRectForContentRect:NSMakeRect(0, 0, sz.width, sz.height)].size;
                [self.window setFrame:f display:YES];
                CGImageRef img = (__bridge CGImageRef)self.view.layer.contents;
                long w = img ? (long)CGImageGetWidth(img) : 0, h = img ? (long)CGImageGetHeight(img) : 0;
                uint32_t centre = 0;
                if (img) {
                    CFDataRef d = CGDataProviderCopyData(CGImageGetDataProvider(img));
                    const size_t row = CGImageGetBytesPerRow(img);
                    std::memcpy(&centre, CFDataGetBytePtr(d) + (size_t)(h / 2) * row + (size_t)(w / 2) * 4, 4);
                    CFRelease(d);
                }
                const NSSize px = [self.view convertSizeToBacking:self.view.bounds.size];
                printf("resize %.0fx%.0f px: shown frame %ldx%ld, centre pixel %08x%s\n", px.width, px.height, w, h,
                       centre, (w == (long)px.width && h == (long)px.height && (centre & 0xFFFFFF)) ? "" : "  BAD");
            }
            [self.window setFrame:start display:YES];
        },
        ^{  // a frame that redraws only the logo box must equal a full frame
            const int d = appCheckPartialFrame();
            printf("logo-only frame vs full frame: %s\n", d == 0 ? "identical" : d < 0 ? "NOT TESTED" : "DIFFERENT");
            const long t = [self.view checkLayerTree];
            printf("window layers after a logo-only frame vs the frame: %s (%ld px)\n",
                   t == 0 ? "identical" : t < 0 ? "NOT TESTED" : "DIFFERENT", t);
        },
        ^{  // frame cost: full frames for FT_BENCH seconds at the window's size (FT_BENCH=1 only - it blocks the main thread)
            if (!getenv("FT_BENCH")) return;
            const NSSize px = [self.view convertSizeToBacking:self.view.bounds.size];
            const double t0 = NSProcessInfo.processInfo.systemUptime;
            int frames = 0;
            while (NSProcessInfo.processInfo.systemUptime - t0 < std::max(1.0, atof(getenv("FT_BENCH")))) {
                appSetDirty();
                appDrawFrame();
                CGImageRelease(appCopyFrame());
                ++frames;
            }
            const double ms = (NSProcessInfo.processInfo.systemUptime - t0) * 1000.0 / frames;
            printf("bench %.0fx%.0f px: %d frames, %.2f ms per frame (%.0f%% of one core at 60 Hz)\n", px.width,
                   px.height, frames, ms, ms * 60.0 / 10.0);
            // idle: only the logo demo moves, so each frame redraws the logo box
            appSetDirty();
            appDrawFrame();
            const double t1 = NSProcessInfo.processInfo.systemUptime;
            int idle = 0;
            while (NSProcessInfo.processInfo.systemUptime - t1 < 1.0) {
                appDrawFrame();
                CGImageRelease(appCopyFrame());
                ++idle;
            }
            const double ims = (NSProcessInfo.processInfo.systemUptime - t1) * 1000.0 / idle;
            printf("bench idle (logo only): %.2f ms per frame (%.0f%% of one core at 60 Hz)\n", ims, ims * 6.0);
            // and then stay up FT_IDLE seconds with the real timers (the CPU use can be read from outside)
            if (const char* idleFor = getenv("FT_IDLE")) {
                const double until = NSProcessInfo.processInfo.systemUptime + atof(idleFor);
                while (NSProcessInfo.processInfo.systemUptime < until)
                    [NSRunLoop.mainRunLoop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
            }
        },
        ^{ [NSApp terminate:nil]; },
    ];
    for (NSUInteger i = 0; i < steps.count; ++i)
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)((i + 1) * 0.3 * NSEC_PER_SEC)), dispatch_get_main_queue(),
                       steps[i]);
}

- (void)openDocument:(id)sender {
    appRequestOpen();
}

- (void)application:(NSApplication*)app openURLs:(NSArray<NSURL*>*)urls {
    if (!self.view) {  // before the window exists: queue
        if (!self.pendingOpen) self.pendingOpen = [NSMutableArray new];
        [self.pendingOpen addObjectsFromArray:urls];
        return;
    }
    if (urls.count) appOpenFile(urls[0].fileSystemRepresentation, true);
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app {
    return YES;
}

// Closing the window or Cmd+Q with unsaved changes: the screen's SAVE / DISCARD / CANCEL question first
// (main.cpp WM_CLOSE)
- (BOOL)windowShouldClose:(NSWindow*)w {
    if (self.closeConfirmed || self.inputTestDir) return YES;
    __weak FTAppDelegate* weakSelf = self;
    appConfirmClose([weakSelf]() {
        weakSelf.closeConfirmed = YES;
        [weakSelf.window close];
    });
    return NO;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)app {
    if (self.closeConfirmed || self.inputTestDir || !appHasUnsavedChanges()) return NSTerminateNow;
    [self.window makeKeyAndOrderFront:nil];
    appConfirmClose([]() {
        dispatch_async(dispatch_get_main_queue(), ^{
            FTAppDelegate* d = (FTAppDelegate*)NSApp.delegate;
            d.closeConfirmed = YES;
            [NSApp terminate:nil];
        });
    });
    return NSTerminateCancel;
}

- (void)applicationWillTerminate:(NSNotification*)n {
    [self.view stop];
    NSString* ini = self.inputTestDir ? [self.inputTestDir stringByAppendingPathComponent:@"input-test.ini"] : iniPath();
    [self saveWindowPlacement:ini];
    appQuit(ini.fileSystemRepresentation);
}
@end

int main(int argc, char** argv) {
    @autoreleasepool {
        if (int shot = appMainStart(argc, argv); shot >= 0) return shot;
        NSApplication* app = [NSApplication sharedApplication];
        app.activationPolicy = NSApplicationActivationPolicyRegular;
        FTAppDelegate* d = [FTAppDelegate new];
        app.delegate = d;
        [app run];
    }
    return 0;
}
