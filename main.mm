//
//  main.mm
//  HybridRenderingPlayground
//
//  Created by Neskol on 2026.04.07.
//

#import <Cocoa/Cocoa.h>
#import <MetalKit/MetalKit.h>
#import "Mtl_Engine.h"

@interface Mtl_EngineViewDelegate : NSObject <MTKViewDelegate>
- (instancetype)initWithMtl_Engine:(Mtl_Engine *)Mtl_Engine;
@end

@implementation Mtl_EngineViewDelegate {
    Mtl_Engine* _Mtl_Engine;
}

- (instancetype)initWithMtl_Engine:(Mtl_Engine *)Mtl_Engine {
    self = [super init];
    if (self) {
        _Mtl_Engine = Mtl_Engine;
    }
    return self;
}

- (void)drawInMTKView:(MTKView *)view {
    MTLRenderPassDescriptor* rpd = view.currentRenderPassDescriptor;
    id<CAMetalDrawable> drawable = view.currentDrawable;
    if (rpd && drawable) {
        _Mtl_Engine->draw((__bridge MTL::RenderPassDescriptor*)rpd, 
                        (__bridge CA::Drawable*)drawable);
    }
}

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size {
    _Mtl_Engine->resize(size.width, size.height);
}

- (void)dealloc {
    delete _Mtl_Engine;
}

@end

@interface AppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation AppDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    return YES;
}
@end

int main(int argc, const char * argv[]) {
    @autoreleasepool {
        if (getenv("MTL_HUD_ENABLED") == NULL) {
            setenv("MTL_HUD_ENABLED", "1", 1);
        }
        NSApplication *app = [NSApplication sharedApplication];
        AppDelegate *appDelegate = [[AppDelegate alloc] init];
        app.delegate = appDelegate;
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];

        NSMenu *mainMenu = [[NSMenu alloc] init];
        NSMenuItem *appMenuItem = [[NSMenuItem alloc] init];
        [mainMenu addItem:appMenuItem];
        [app setMainMenu:mainMenu];

        NSMenu *appMenu = [[NSMenu alloc] init];
        NSString *appName = [[NSProcessInfo processInfo] processName];
        NSString *quitTitle = [@"Quit " stringByAppendingString:appName];
        NSMenuItem *quitMenuItem = [[NSMenuItem alloc] initWithTitle:quitTitle
                                                              action:@selector(terminate:)
                                                       keyEquivalent:@"q"];
        [appMenu addItem:quitMenuItem];
        [appMenuItem setSubmenu:appMenu];

        NSRect frame = NSMakeRect(0, 0, 1280, 720);

        NSWindow *window = [[NSWindow alloc]
            initWithContentRect:frame
                      styleMask:NSWindowStyleMaskTitled |
                                NSWindowStyleMaskClosable |
                                NSWindowStyleMaskResizable
                        backing:NSBackingStoreBuffered
                          defer:NO];

        window.title = @"Hybrid Rendering Playground";

        MTKView *view = [[MTKView alloc] initWithFrame:frame];
        window.contentView = view;

        Mtl_Engine *engine = reinterpret_cast<Mtl_Engine*>(CreateEngine((__bridge void*)view));
        
        view.device = (__bridge id<MTLDevice>)engine->getDevice();
        view.preferredFramesPerSecond = engine->getTargetFPS();
        view.clearColor = MTLClearColorMake(0.1, 0.1, 0.1, 1.0);
        view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
        view.depthStencilPixelFormat = MTLPixelFormatDepth32Float;

        CAMetalLayer *layer = (CAMetalLayer *)view.layer;
        layer.displaySyncEnabled = NO;

        Mtl_EngineViewDelegate *delegate = [[Mtl_EngineViewDelegate alloc] initWithMtl_Engine:engine];
        view.delegate = delegate;

        [window center];
        [window makeKeyAndOrderFront:nil];
        [app activateIgnoringOtherApps:YES];
        [app run];
    }
}
