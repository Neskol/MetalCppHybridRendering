/*
See the LICENSE.txt file for this sample’s licensing information.

Abstract:
The header for the app delegate.
*/
#if TARGET_IOS

#import <UIKit/UIKit.h>

@interface AAPLAppDelegate : UIResponder <UIApplicationDelegate>

@end

#else

#import <Cocoa/Cocoa.h>

@interface AAPLAppDelegate : NSObject <NSApplicationDelegate>

@end

#endif
