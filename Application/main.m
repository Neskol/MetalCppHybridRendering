/*
See the LICENSE.txt file for this sample’s licensing information.

Abstract:
The app's entry point.
*/

#if defined(TARGET_IOS)
#import <UIKit/UIKit.h>
#import <TargetConditionals.h>
#import <Availability.h>
#import "AAPLAppDelegate.h"
#else
#import <Cocoa/Cocoa.h>
#endif
#include <stdlib.h>

#if defined(TARGET_IOS)

int main(int argc, char * argv[]) {

//#if TARGET_OS_SIMULATOR
//#error This sample does not support the iOS Simulator
//#endif

    @autoreleasepool {
        setenv("MTL_HUD_ENABLED", "1", 1);
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([AAPLAppDelegate class]));
    }
}

#elif defined(TARGET_MACOS)

int main(int argc, const char * argv[]) {
    setenv("MTL_HUD_ENABLED", "1", 1);
    return NSApplicationMain(argc, argv);
}

#endif
