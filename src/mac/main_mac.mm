// FatTracker for macOS: application, window, frame loop and input (AppKit). The screen itself is the shared
// PlayScreen drawing through Gfx/Text/Font on Core Graphics; FTView shows each finished frame as its
// layer's contents and passes keys (as Windows VK codes, src/mac/keymap_mac.cpp), mouse, wheel and dropped
// files to app_glue. --shot <file.png> ... renders offscreen and exits (src/app/shot.cpp).
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <algorithm>
#include <cmath>

#include "mac/app_glue.h"
#include "mac/keymap_mac.h"
#include "platform/vk.h"

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
}

- (instancetype)initWithFrame:(NSRect)r {
    if ((self = [super initWithFrame:r])) {
        self.wantsLayer = YES;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawOnSetNeedsDisplay;
        self.layer.contentsGravity = kCAGravityResize;
        self.layer.magnificationFilter = kCAFilterNearest;
        __weak FTView* weakSelf = self;
        // frame pacing: ~60 Hz; a frame is drawn only when something changed or the screen animates
        _frameTimer = [NSTimer timerWithTimeInterval:1.0 / 60.0
                                             repeats:YES
                                               block:^(NSTimer*) { [weakSelf tick]; }];
        _audioTimer = [NSTimer timerWithTimeInterval:2.0
                                             repeats:YES
                                               block:^(NSTimer*) { appPollAudio(); }];
        _clockTimer = [NSTimer timerWithTimeInterval:1.0 repeats:YES block:^(NSTimer*) { appSetDirty(); }];
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

- (void)syncSize {
    const CGFloat s = self.window ? self.window.backingScaleFactor : 2.0;
    const NSSize b = self.bounds.size;
    const uint32_t w = (uint32_t)std::lround(b.width * s), h = (uint32_t)std::lround(b.height * s);
    if (w && h) appResize(w, h);
    self.layer.contentsScale = s;
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
    char dir[1024];
    if (appTakeOpenRequest(dir, sizeof dir)) [self showOpenPanel:dir];
    if (!appFrameDue()) return;
    char name[512];
    bool edited = false;
    if (appTitle(name, sizeof name, edited)) {
        self.window.title = [NSString stringWithFormat:@"FatTracker \u2014 %@", [NSString stringWithUTF8String:name] ?: @"?"];
        self.window.documentEdited = edited;
    }
    if (appDrawFrame()) [self setNeedsDisplay:YES];
}

- (void)showOpenPanel:(const char*)dir {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    panel.message = @"Open a module";
    if (*dir) panel.directoryURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:dir] ?: @"/" isDirectory:YES];
    [panel beginSheetModalForWindow:self.window
                  completionHandler:^(NSModalResponse r) {
                      if (r == NSModalResponseOK && panel.URL) appOpenChosen(panel.URL.fileSystemRepresentation);
                  }];
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
             options:NSTrackingMouseMoved | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
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
    if (vk) appKey(vk, shift, ctrl, alt, false, e.isARepeat);
    if (f & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) return;  // Ctrl+key types nothing
    NSString* chars = e.characters;
    for (NSUInteger i = 0; i < chars.length; ++i) {
        const unichar c = [chars characterAtIndex:i];
        if (c < 32 || c == 127 || (c >= 0xF700 && c <= 0xF8FF)) continue;  // controls, arrows, F-keys
        if (alt && c < 128) continue;  // Option+key is a shortcut (Windows drops Alt characters); letters like ą still type
        appChar(c);
    }
}

- (void)keyUp:(NSEvent*)e {
    if (const unsigned vk = vkFromMac(e.keyCode)) appKeyUp(vk);
}

// A modifier alone sends no keyDown: compare the device bits to see which one went down or up. Right
// Option = PLAY song, right Control = PLAY pattern (extended, like the right keys on Windows).
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
        case 0x36: mask = kRightCmd; vk = VK_CONTROL; break;
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
    [CATransaction commit];
    CGImageRelease(img);
}

- (void)stop {
    [_frameTimer invalidate];
    [_audioTimer invalidate];
    [_clockTimer invalidate];
}
@end

// ---------------------------------------------------------------- application

@interface FTAppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property(copy) NSString* inputTestDir;  // --input-test <dir>: scripted events, PNGs, quit (no audio)
@property(strong) NSWindow* window;
@property(strong) FTView* view;
@property(strong) NSMutableArray<NSURL*>* pendingOpen;
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

- (void)applicationDidFinishLaunching:(NSNotification*)n {
    [self buildMenu];
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
    [self.window center];
    [self.view syncSize];

    NSArray<NSString*>* args = NSProcessInfo.processInfo.arguments;
    if (args.count >= 3 && [args[1] isEqualToString:@"--input-test"]) self.inputTestDir = args[2];
    // the input test runs without the user's settings file and silently
    NSString* ini = self.inputTestDir ? [self.inputTestDir stringByAppendingPathComponent:@"input-test.ini"] : iniPath();
    appWindowReady(ini.fileSystemRepresentation, replayDir().fileSystemRepresentation, !self.inputTestDir);
    // FatTracker <module> from the command line, or files opened from the Finder before launch finished
    if (args.count >= 2 && ![args[1] hasPrefix:@"-"]) appOpenFile(args[1].fileSystemRepresentation, true);
    for (NSURL* u in self.pendingOpen) appOpenFile(u.fileSystemRepresentation, true);
    [self.pendingOpen removeAllObjects];

    [self.window makeKeyAndOrderFront:nil];
    [self.window makeFirstResponder:self.view];
    [NSApp activateIgnoringOtherApps:YES];
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
    const NSEventModifierFlags cmd = NSEventModifierFlagCommand, ctl = NSEventModifierFlagControl;
    NSArray* steps = @[
        ^{  // edit mode, three piano keys (Z S X = C D C#... of the base octave), Cmd+Z undoes the last one
            [self key:0x31 chars:@" " flags:0];
            [self key:0x06 chars:@"z" flags:0];
            [self key:0x01 chars:@"s" flags:0];
            [self key:0x07 chars:@"x" flags:0];
            [self key:0x06 chars:@"z" flags:cmd];
        },
        ^{ [self shot:@"1_keys.png"]; },
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

- (void)applicationWillTerminate:(NSNotification*)n {
    [self.view stop];
    NSString* ini = self.inputTestDir ? [self.inputTestDir stringByAppendingPathComponent:@"input-test.ini"] : iniPath();
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
