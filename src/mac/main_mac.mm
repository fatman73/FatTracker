// FatTracker for macOS: application, window and frame loop (AppKit). The screen itself is the shared
// PlayScreen drawing through Gfx/Text/Font on Core Graphics; FTView shows each finished frame as its
// layer's contents. --shot <file.png> ... renders offscreen and exits (src/app/shot.cpp).
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <algorithm>
#include <cmath>

#include "mac/app_glue.h"

namespace {
NSString* appSupportDir() {
    NSArray* a = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
    NSString* base = a.count ? a[0] : NSHomeDirectory();
    return [base stringByAppendingPathComponent:@"FatTracker"];
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
    if (!appFrameDue()) return;
    if (appDrawFrame()) [self setNeedsDisplay:YES];
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

    appWindowReady(iniPath().fileSystemRepresentation, replayDir().fileSystemRepresentation);
    // FatTracker <module> from the command line, or files opened from the Finder before launch finished
    NSArray<NSString*>* args = NSProcessInfo.processInfo.arguments;
    if (args.count >= 2 && ![args[1] hasPrefix:@"-"]) appOpenFile(args[1].fileSystemRepresentation, true);
    for (NSURL* u in self.pendingOpen) appOpenFile(u.fileSystemRepresentation, true);
    [self.pendingOpen removeAllObjects];

    [self.window makeKeyAndOrderFront:nil];
    [self.window makeFirstResponder:self.view];
    [NSApp activateIgnoringOtherApps:YES];
    appSetDirty();
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
    appQuit(iniPath().fileSystemRepresentation);
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
