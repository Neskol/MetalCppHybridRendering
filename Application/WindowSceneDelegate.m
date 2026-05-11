/*
See the LICENSE.txt file for this sample’s licensing information.

Abstract:
The implementation for the iOS and tvOS window scene delegate.
*/

#import "WindowSceneDelegate.h"

#if TARGET_OS_IOS || TARGET_OS_TV

@implementation WindowSceneDelegate

/// Configures the app's window property and attaches it to a scene.
///
/// - Parameters:
///   - scene: A scene that's connecting to the app.
///   - session: A session instance that has the configuration details for `scene`.
///   - options: Additional options for configuring the scene, such as when a person
///   selects a quick action.
///
/// This method is an opportunity for the app to configure the `window` property
/// and attach it to the `scene` argument.
/// If the app has a storyboard, UIKit automatically configures the
/// `window` property and attaches it to `scene` before calling this method.
///
/// > Note: The `scene` and `session` arguments might not be new instances
/// > (see `application:configurationForConnectingSceneSession`).
- (void)       scene:(UIScene *)scene
willConnectToSession:(UISceneSession *)session
             options:(UISceneConnectionOptions *)connectionOptions {

    if (![scene isKindOfClass:[UIWindowScene class]]) {
        return;
    }
}

/// Notifies the app when UIKit disconnects a scene.
///
/// - Parameter scene: A scene that UIKit is disconnecting from the app.
///
/// This method is an opportunity for the app to release any resources that it
/// associates with `scene` that it can recreate if UIKit reconnects the scene.
///
/// > Note: UIKit can reconnect a scene unless it discards the scene's sessions
/// > (see `application:didDiscardSceneSessions`).
///
/// UIKit calls this method when it releases a scene shortly after it enters the
/// background or when it discards the scene's session.
- (void)sceneDidDisconnect:(UIScene *)scene {
    // Release resources for `scene` that the app can create again later.
}

@end

#endif
