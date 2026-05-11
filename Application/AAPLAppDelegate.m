/*
See the LICENSE.txt file for this sample’s licensing information.

Abstract:
The implementation of the app delegate.
*/

#import "AAPLAppDelegate.h"

#if TARGET_IOS

@implementation AAPLAppDelegate

/// Returns a configuration for UIKit when it creates a new scene.
///
/// - Parameters:
///   - application: The app's `UIApplication` singleton.
///   - connectingSceneSession: A session instance that contains configuration data
///   from the app's `Info.plist` file, if applicable.
///   - options: The system-specific options that configure the scene.
///
/// UIKit calls this method when it's creating a new session and applies the configuration
/// it returns.
- (UISceneConfiguration *)application:(UIApplication *)application configurationForConnectingSceneSession:(UISceneSession *)connectingSceneSession options:(UISceneConnectionOptions *)options {

    return [[UISceneConfiguration alloc] initWithName:@"Default Configuration"
                                          sessionRole:connectingSceneSession.role];
}

/// Notifies the app when UIKit discards scene sessions.
/// - Parameters:
///   - application: The app's `UIApplication` singleton.
///   - sceneSessions: A set of sessions that UIKit is discarding.
///
/// UIKit calls this method when a person discards a scene session,
/// or after launch if they discard the scene while the app isn't running.
- (void)    application:(UIApplication *)application
didDiscardSceneSessions:(NSSet<UISceneSession *> *)sceneSessions {
    // Release any resources specific to any scene in the `sceneSessions` set.
}

@end

#else

@implementation AAPLAppDelegate

- (BOOL) applicationShouldTerminateAfterLastWindowClosed:(NSApplication*) sender
{
    return YES;
}

@end

#endif
