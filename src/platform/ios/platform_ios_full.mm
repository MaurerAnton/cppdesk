/**
 * platform_ios_full.mm — Comprehensive iOS platform implementation
 *
 * This file implements the full iOS platform backend for cppdesk,
 * covering screen broadcast, app lifecycle, networking, notifications,
 * background tasks, file sharing, clipboard, battery/device info,
 * audio sessions, touch/gesture handling, and more — all via direct
 * UIKit / ReplayKit / AVFoundation / NetworkExtension API calls.
 *
 * Features:
 *   1.  Screen broadcast via ReplayKit Broadcast Upload Extension   (§ Screen)
 *   2.  UIKit app lifecycle (UIApplicationDelegate)                 (§ Lifecycle)
 *   3.  Network extension for NEPacketTunnelProvider                (§ Network)
 *   4.  Local notification scheduling (UNUserNotificationCenter)    (§ Notify)
 *   5.  Background task management (beginBackgroundTask)            (§ BgTask)
 *   6.  File sharing via UIDocumentPickerViewController             (§ FileShare)
 *   7.  iOS clipboard access (UIPasteboard)                         (§ Clipboard)
 *   8.  Battery and device info (UIDevice)                          (§ Device)
 *   9.  Audio session management (AVAudioSession)                   (§ Audio)
 *  10.  Touch/gesture handling (UIGestureRecognizer)                (§ Touch)
 *  11.  Reachability / network monitoring                          (§ Reachability)
 *  12.  Keychain secure storage                                     (§ Keychain)
 *  13.  System-wide keyboard input                                  (§ Keyboard)
 *  14.  Screen mirroring / AirPlay detection                       (§ AirPlay)
 *  15.  Platform manager / C API export                            (§ Manager)
 *
 *  Target:  C++20, spdlog logging, iOS 14+.
 *  Guarded  by #ifdef __APPLE__ && TARGET_OS_IOS.
 */

// ===========================================================================
//  Preamble
// ===========================================================================
#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_IOS

// ---- C / C++ Standard Library ----
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

#include <spdlog/spdlog.h>

// ---- System frameworks ----
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <TargetConditionals.h>

// ---- Objective-C Runtime & Foundation ----
#import <Foundation/Foundation.h>
#import <Foundation/NSProcessInfo.h>
#import <Foundation/NSUserDefaults.h>

// ---- UIKit ----
#import <UIKit/UIKit.h>
#import <UIKit/UIGestureRecognizer.h>
#import <UIKit/UIPasteboard.h>
#import <UIKit/UIDocumentPickerViewController.h>
#import <UIKit/UIScreen.h>
#import <UIKit/UIWindow.h>
#import <UIKit/UIWindowScene.h>
#import <UIKit/UIScene.h>

// ---- ReplayKit ----
#import <ReplayKit/ReplayKit.h>
#import <ReplayKit/RPBroadcastPickerView.h>
#import <ReplayKit/RPBroadcastActivityViewController.h>
#import <ReplayKit/RPBroadcastController.h>
#import <ReplayKit/RPBroadcastHandler.h>
#import <ReplayKit/RPScreenRecorder.h>

// ---- AVFoundation ----
#import <AVFoundation/AVFoundation.h>
#import <AVFoundation/AVAudioSession.h>
#import <AVFoundation/AVAudioPlayer.h>
#import <AVFoundation/AVAudioRecorder.h>
#import <AVFoundation/AVCaptureSession.h>
#import <AVFoundation/AVCaptureDevice.h>
#import <AVFoundation/AVCaptureInput.h>
#import <AVFoundation/AVCaptureOutput.h>

// ---- UserNotifications ----
#import <UserNotifications/UserNotifications.h>

// ---- NetworkExtension ----
#import <NetworkExtension/NetworkExtension.h>
#import <NetworkExtension/NEPacketTunnelProvider.h>
#import <NetworkExtension/NEPacketTunnelNetworkSettings.h>
#import <NetworkExtension/NEIPv4Settings.h>
#import <NetworkExtension/NEDNSSettings.h>
#import <NetworkExtension/NETunnelProviderSession.h>
#import <NetworkExtension/NEVPNConnection.h>

// ---- SystemConfiguration (Reachability) ----
#import <SystemConfiguration/SystemConfiguration.h>

// ---- Security (Keychain) ----
#import <Security/Security.h>

// ---- CoreTelephony (optional, for carrier info) ----
#if __has_include(<CoreTelephony/CTTelephonyNetworkInfo.h>)
#import <CoreTelephony/CTTelephonyNetworkInfo.h>
#import <CoreTelephony/CTCarrier.h>
#endif

// ---- UniformTypeIdentifiers ----
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

// ---- CoreGraphics ----
#import <CoreGraphics/CoreGraphics.h>

// ---------------------------------------------------------------------------
//  Convenience macros
// ---------------------------------------------------------------------------
#ifndef NDEBUG
#define IOS_LOGV(...) spdlog::debug(__VA_ARGS__)
#else
#define IOS_LOGV(...) ((void)0)
#endif
#define IOS_LOGD(...) spdlog::debug(__VA_ARGS__)
#define IOS_LOGI(...) spdlog::info(__VA_ARGS__)
#define IOS_LOGW(...) spdlog::warn(__VA_ARGS__)
#define IOS_LOGE(...) spdlog::error(__VA_ARGS__)

// ===========================================================================
//  namespace cppdesk::platform::ios
// ===========================================================================
namespace cppdesk::platform::ios {

// ---------------------------------------------------------------------------
//  Forward declarations
// ---------------------------------------------------------------------------
struct ScreenBroadcast;
struct AppLifecycleDelegate;
struct PacketTunnelProvider;
struct NotificationScheduler;
struct BackgroundTaskManager;
struct DocumentPicker;
struct ClipboardManager;
struct DeviceInfo;
struct AudioSessionManager;
struct TouchHandler;
struct ReachabilityMonitor;
struct KeychainStore;
struct KeyboardController;
struct AirPlayMonitor;
struct IOSPlatformManager;

// ===========================================================================
//  SECTION 1 — OBJC-HELPER INFRASTRUCTURE
// ===========================================================================
//
//  These C++ wrapper functions bridge Objective-C objects into the C++ world.
//  They handle ARC bridging (__bridge, __bridge_retained, __bridge_transfer)
//  and convert between NSString/std::string.

/**
 * Convert a std::string to an NSString.
 */
inline NSString *std_to_ns(const std::string &s) {
    return [NSString stringWithUTF8String:s.c_str()];
}

/**
 * Convert an NSString to a std::string.
 */
inline std::string ns_to_std(NSString *ns) {
    if (!ns) return {};
    return std::string([ns UTF8String]);
}

/**
 * Convert a std::string_view to an NSString.
 */
inline NSString *sv_to_ns(std::string_view sv) {
    return [[NSString alloc] initWithBytes:sv.data()
                                    length:sv.size()
                                  encoding:NSUTF8StringEncoding];
}

/**
 * RAII helper that bridges an ObjC block to keep an object alive.
 */
template <typename T>
struct ObjCRetainGuard {
    T obj;
    explicit ObjCRetainGuard(T o) : obj(o) { if (obj) CFRetain((__bridge CFTypeRef)obj); }
    ~ObjCRetainGuard() { if (obj) CFRelease((__bridge CFTypeRef)obj); }
    ObjCRetainGuard(const ObjCRetainGuard &) = delete;
    ObjCRetainGuard &operator=(const ObjCRetainGuard &) = delete;
    ObjCRetainGuard(ObjCRetainGuard &&other) noexcept : obj(other.obj) { other.obj = nil; }
    ObjCRetainGuard &operator=(ObjCRetainGuard &&other) noexcept {
        if (this != &other) { if (obj) CFRelease((__bridge CFTypeRef)obj); obj = other.obj; other.obj = nil; }
        return *this;
    }
};

/**
 * Run a block on the main dispatch queue, synchronously or asynchronously.
 */
inline void dispatch_main_sync(std::function<void()> fn) {
    if ([NSThread isMainThread]) {
        fn();
    } else {
        dispatch_sync(dispatch_get_main_queue(), ^{ fn(); });
    }
}

inline void dispatch_main_async(std::function<void()> fn) {
    dispatch_async(dispatch_get_main_queue(), ^{ fn(); });
}

// ===========================================================================
//  SECTION 2 — SCREEN BROADCAST via ReplayKit
// ===========================================================================
//
//  ReplayKit Broadcast Upload Extension allows capturing the device screen
//  and streaming it to a remote endpoint.  This is the primary screen-capture
//  mechanism on iOS (no rootless direct framebuffer access exists).
//
//  Architecture:
//   1. RPBroadcastPickerView —  system UI for the user to start broadcast
//   2. RPScreenRecorder        —  controls recording / broadcast session
//   3. RPBroadcastHandler       —  base class for custom broadcast handler
//       (subclassed in the Broadcast Upload Extension target)
//   4. CMSampleBuffer           —  video/audio frames arrive as sample buffers
//
//  In the main app, we present the picker and observe broadcast state.
//  In the extension, we process CMSampleBuffers and encode/transmit.

/**
 * C++ wrapper around RPScreenRecorder and RPBroadcastActivityViewController.
 */
struct ScreenBroadcast {
    // --- RPScreenRecorder singleton ---
    RPScreenRecorder *recorder = nil;

    // --- Broadcast controller (iOS 13+) ---
    RPBroadcastController *broadcastController = nil;

    // --- Broadcast state ---
    std::atomic<bool> broadcasting{false};
    std::atomic<bool> paused{false};

    // --- Callbacks ---
    using FrameCallback = std::function<void(const std::vector<uint8_t> &data,
                                              int width, int height,
                                              int64_t timestamp_us)>;
    using StateCallback = std::function<void(bool active, const std::string &error)>;

    FrameCallback on_frame;
    StateCallback  on_state_change;

    // --- Capabilities ---
    bool is_screen_recording_available() const {
        return [RPScreenRecorder class] != nil &&
               [RPScreenRecorder sharedRecorder].isAvailable;
    }

    bool is_broadcast_available() const {
        // iOS 12+ has broadcast; iOS 11 requires ReplayKit Live
        if (@available(iOS 12.0, *)) {
            return true;
        }
        return false;
    }

    // --- Start screen recording (in-app capture, iOS 11+) ---
    bool start_in_app_capture(bool microphone_enabled, std::string &error_out) {
        recorder = [RPScreenRecorder sharedRecorder];

        if (!recorder.isAvailable) {
            error_out = "RPScreenRecorder not available on this device";
            IOS_LOGE("[ios] RPScreenRecorder unavailable");
            return false;
        }

        recorder.microphoneEnabled = microphone_enabled ? YES : NO;

        // Set up video/audio format preferences
        if (@available(iOS 11.0, *)) {
            recorder.cameraEnabled = NO; // we don't want PiP camera overlay
        }

        __block bool started = false;
        __block std::string err_str;
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);

        [recorder startRecordingWithHandler:^(NSError * _Nullable error) {
            if (error) {
                err_str = ns_to_std(error.localizedDescription);
                IOS_LOGE("[ios] startRecording failed: {}", err_str);
            } else {
                started = true;
                broadcasting.store(true);
                IOS_LOGI("[ios] In-app screen recording started");
            }
            dispatch_semaphore_signal(sem);
        }];

        dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);

        if (!started) {
            error_out = err_str;
        }
        return started;
    }

    // --- Stop in-app recording ---
    bool stop_in_app_capture() {
        __block bool stopped = false;
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);

        [recorder stopRecordingWithHandler:^(RPPreviewViewController * _Nullable preview,
                                              NSError * _Nullable error) {
            if (error) {
                IOS_LOGE("[ios] stopRecording failed: {}",
                          ns_to_std(error.localizedDescription));
            } else {
                stopped = true;
                broadcasting.store(false);
                IOS_LOGI("[ios] In-app screen recording stopped");

                // The preview VC can be presented to let user edit/share
                if (preview && on_state_change) {
                    on_state_change(false, "Preview available");
                }
            }
            dispatch_semaphore_signal(sem);
        }];

        dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
        return stopped;
    }

    // --- Discard recording without preview ---
    void discard_in_app_recording() {
        [recorder discardRecordingWithHandler:^{
            IOS_LOGI("[ios] Recording discarded");
            broadcasting.store(false);
        }];
    }

    // --- Start broadcast via RPBroadcastController (iOS 13+) ---
    bool start_broadcast(std::string &error_out) {
        if (@available(iOS 13.0, *)) {
            __block bool started = false;
            __block std::string err_str;
            dispatch_semaphore_t sem = dispatch_semaphore_create(0);

            [RPBroadcastController startBroadcastWithHandler:^(RPBroadcastController * _Nullable controller,
                                                                NSError * _Nullable error) {
                if (error) {
                    err_str = ns_to_std(error.localizedDescription);
                    IOS_LOGE("[ios] startBroadcast failed: {}", err_str);
                } else {
                    self->broadcastController = controller;
                    started = true;
                    self->broadcasting.store(true);
                    IOS_LOGI("[ios] Broadcast started successfully");
                }
                dispatch_semaphore_signal(sem);
            }];

            dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);

            if (!started) {
                error_out = err_str;
            }
            return started;
        } else {
            error_out = "RPBroadcastController requires iOS 13+";
            return false;
        }
    }

    // --- Pause broadcast ---
    bool pause_broadcast() {
        if (@available(iOS 13.0, *)) {
            if (!broadcastController) return false;
            if (broadcastController.isPaused) return true;

            [broadcastController pauseBroadcast];
            paused.store(true);
            IOS_LOGI("[ios] Broadcast paused");
            return true;
        }
        return false;
    }

    // --- Resume broadcast ---
    bool resume_broadcast() {
        if (@available(iOS 13.0, *)) {
            if (!broadcastController) return false;
            if (!broadcastController.isPaused) return true;

            [broadcastController resumeBroadcast];
            paused.store(false);
            IOS_LOGI("[ios] Broadcast resumed");
            return true;
        }
        return false;
    }

    // --- Stop broadcast ---
    bool stop_broadcast() {
        if (@available(iOS 13.0, *)) {
            if (!broadcastController) return false;

            [broadcastController finishBroadcastWithHandler:^(NSError * _Nullable error) {
                if (error) {
                    IOS_LOGE("[ios] finishBroadcast error: {}",
                              ns_to_std(error.localizedDescription));
                }
                self->broadcasting.store(false);
                self->paused.store(false);
                self->broadcastController = nil;
                IOS_LOGI("[ios] Broadcast finished");
            }];
            return true;
        }
        return false;
    }

    // --- Get broadcast service info ---
    struct BroadcastInfo {
        std::string bundle_id;
        std::string service_name;
        bool        is_broadcasting;
        bool        is_paused;
    };

    BroadcastInfo get_info() const {
        BroadcastInfo info{};
        info.is_broadcasting = broadcasting.load();
        info.is_paused = paused.load();

        if (@available(iOS 13.0, *)) {
            if (broadcastController) {
                info.is_broadcasting = broadcastController.isBroadcasting;
                info.is_paused = broadcastController.isPaused;
                info.bundle_id = ns_to_std(broadcastController.broadcastURL.absoluteString);
            }
        }
        info.service_name = ns_to_std(recorder.cameraPreviewView ?
                                       @"Camera" : @"ScreenOnly");
        return info;
    }
};

// ===========================================================================
//  SECTION 3 — BROADCAST UPLOAD EXTENSION (SAMPLE HANDLER)
// ===========================================================================
//
//  The Broadcast Upload Extension runs in a separate process and receives
//  CMSampleBuffer objects from the system.  This code would live in the
//  extension target, but we include the interface here for completeness.

/**
 * @interface CppDeskBroadcastHandler : RPBroadcastHandler
 *
 * Custom broadcast handler that receives sample buffers and passes them
 * to the C++ layer for encoding / transmission.
 *
 * In a real project, this class is compiled into the Broadcast Upload
 * Extension target.  We declare it here as documentation and for code
 * generation / interface definitions.
 */
@interface CppDeskBroadcastHandler : RPBroadcastHandler
@end

@implementation CppDeskBroadcastHandler {
    // C++ state held via __bridge
    void *_cpp_context;  // pointer to ScreenBroadcastCaptureSession*
}

/**
 * Called when the broadcast service is created.  Use this to set up
 * encoders, network connections, etc.
 */
- (void)broadcastStartedWithSetupInfo:(NSDictionary<NSString *, NSObject *> *)setupInfo {
    IOS_LOGI("[ios] Broadcast Upload Extension started");
    IOS_LOGD("[ios] Setup info keys: {}",
              ns_to_std(setupInfo.description ?: @"(none)"));

    // Extract user-specific info passed from main app via setupInfo
    NSString *endpointURL = (NSString *)setupInfo[@"endpointURL"];
    NSString *streamKey   = (NSString *)setupInfo[@"streamKey"];

    if (endpointURL) {
        IOS_LOGI("[ios] Endpoint URL: {}", ns_to_std(endpointURL));
    }

    // Here we would initialise the video encoder and network transport.
    // For now we log that the broadcast is ready.
}

/**
 * Called when the broadcast finishes (user stops or system kills).
 */
- (void)broadcastFinished {
    IOS_LOGI("[ios] Broadcast Upload Extension finished");

    // Tear down encoders, flush remaining data, close connections.
}

/**
 * Called for each video sample buffer.
 *
 * The CMSampleBuffer may contain raw YUV or encoded data depending on
 * the system configuration.  We extract planes and pass to C++.
 */
- (void)processSampleBuffer:(CMSampleBufferRef)sampleBuffer
                   withType:(RPSampleBufferType)sampleBufferType {

    switch (sampleBufferType) {
    case RPSampleBufferTypeVideo: {
        // --- Video frame ---
        CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
        if (!imageBuffer) {
            IOS_LOGW("[ios] No image buffer in video sample");
            return;
        }

        CVPixelBufferLockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);

        size_t width  = CVPixelBufferGetWidth(imageBuffer);
        size_t height = CVPixelBufferGetHeight(imageBuffer);
        OSType format = CVPixelBufferGetPixelFormatType(imageBuffer);

        // Extract Y plane (NV12 / 420v / 420f)
        if (CVPixelBufferIsPlanar(imageBuffer)) {
            size_t planeCount = CVPixelBufferGetPlaneCount(imageBuffer);
            for (size_t p = 0; p < planeCount; ++p) {
                void *baseAddr   = CVPixelBufferGetBaseAddressOfPlane(imageBuffer, p);
                size_t rowBytes  = CVPixelBufferGetBytesPerRowOfPlane(imageBuffer, p);
                size_t planeH    = CVPixelBufferGetHeightOfPlane(imageBuffer, p);
                (void)baseAddr; (void)rowBytes; (void)planeH;
            }
        } else {
            void *baseAddr  = CVPixelBufferGetBaseAddress(imageBuffer);
            size_t rowBytes = CVPixelBufferGetBytesPerRow(imageBuffer);
            (void)baseAddr; (void)rowBytes;
        }

        // Get presentation timestamp
        CMTime pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
        int64_t pts_us = CMTimeGetSeconds(pts) * 1'000'000;

        IOS_LOGV("[ios] Video frame: {}x{} fmt={:#x} pts={}us",
                  width, height, (unsigned)format, pts_us);

        CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
        break;
    }

    case RPSampleBufferTypeAudioApp:
    case RPSampleBufferTypeAudioMic: {
        // --- Audio sample ---
        CMBlockBufferRef blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer);
        if (!blockBuffer) {
            IOS_LOGW("[ios] No block buffer in audio sample");
            return;
        }

        size_t lengthAtOffset = 0;
        size_t totalLength    = 0;
        char  *dataPtr        = nullptr;

        OSStatus status = CMBlockBufferGetDataPointer(blockBuffer, 0,
                                                       &lengthAtOffset,
                                                       &totalLength,
                                                       &dataPtr);
        if (status == kCMBlockBufferNoErr && dataPtr) {
            CMTime pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
            int64_t pts_us = CMTimeGetSeconds(pts) * 1'000'000;
            (void)pts_us;
            IOS_LOGV("[ios] Audio sample: {} bytes, type={}",
                      totalLength,
                      sampleBufferType == RPSampleBufferTypeAudioMic ? "mic" : "app");
        }
        break;
    }

    default:
        IOS_LOGV("[ios] Unknown sample buffer type: {}", (int)sampleBufferType);
        break;
    }
}

@end  // CppDeskBroadcastHandler

// ===========================================================================
//  SECTION 4 — UIKIT APP LIFECYCLE (UIApplicationDelegate)
// ===========================================================================
//
//  The UIApplicationDelegate protocol is the entry point for iOS app
//  lifecycle management.  We implement a delegate that bridges lifecycle
//  events into C++ callbacks usable by the cppdesk engine.

/**
 * @interface CppDeskAppDelegate : UIResponder <UIApplicationDelegate>
 *
 * Full UIApplicationDelegate implementation for the cppdesk iOS host app.
 */
@interface CppDeskAppDelegate : UIResponder <UIApplicationDelegate>
@property (nonatomic, strong) UIWindow *window;
@end

// --- C++ lifecycle callback storage ---
struct AppLifecycleCallbacks {
    std::function<void()>                    on_did_finish_launching;
    std::function<void()>                    on_will_resign_active;
    std::function<void()>                    on_did_enter_background;
    std::function<void()>                    on_will_enter_foreground;
    std::function<void()>                    on_did_become_active;
    std::function<void()>                    on_will_terminate;
    std::function<void(NSDictionary *)>       on_did_register_for_remote_notifications;
    std::function<void(NSError *)>            on_did_fail_to_register_for_remote_notifications;
    std::function<void(NSDictionary *, std::function<void(UIBackgroundFetchResult)>)> on_did_receive_remote_notification;
    std::function<bool(NSURL *, NSDictionary *)> on_open_url;
    std::function<void(UIScene *, UISceneSession *, UISceneConnectionOptions *)> on_scene_will_connect;
    std::function<void(UIScene *)>           on_scene_did_disconnect;
    std::function<void(UIScene *)>           on_scene_did_become_active;
    std::function<void(UIScene *)>           on_scene_will_resign_active;
    std::function<void(UIScene *)>           on_scene_will_enter_foreground;
    std::function<void(UIScene *)>           on_scene_did_enter_background;
};

static AppLifecycleCallbacks g_lifecycle;

// -------------------------------------------------------------------------
//  C++ Lifecycle Manager
// -------------------------------------------------------------------------
struct AppLifecycleDelegate {

    void set_callbacks(AppLifecycleCallbacks cbs) {
        g_lifecycle = std::move(cbs);
    }

    /** Return current app state as string. */
    std::string app_state_string() const {
        UIApplicationState state = [UIApplication sharedApplication].applicationState;
        switch (state) {
        case UIApplicationStateActive:      return "active";
        case UIApplicationStateInactive:    return "inactive";
        case UIApplicationStateBackground:  return "background";
        default:                            return "unknown";
        }
    }

    bool is_in_foreground() const {
        return [UIApplication sharedApplication].applicationState == UIApplicationStateActive;
    }

    bool is_protected_data_available() const {
        return [UIApplication sharedApplication].isProtectedDataAvailable;
    }

    /** Get the background time remaining (in seconds). */
    double background_time_remaining() const {
        return [UIApplication sharedApplication].backgroundTimeRemaining;
    }

    /** Register for remote notifications (push). */
    void register_for_push_notifications() {
        dispatch_main_async([=]() {
            [[UIApplication sharedApplication]
                registerForRemoteNotifications];
        });
    }

    /** Unregister for remote notifications. */
    void unregister_for_push_notifications() {
        dispatch_main_async([=]() {
            [[UIApplication sharedApplication]
                unregisterForRemoteNotifications];
        });
    }

    /** Get the current device orientation. */
    std::string orientation_string() const {
        UIDeviceOrientation o = [UIDevice currentDevice].orientation;
        switch (o) {
        case UIDeviceOrientationPortrait:           return "portrait";
        case UIDeviceOrientationPortraitUpsideDown: return "portrait_upside_down";
        case UIDeviceOrientationLandscapeLeft:      return "landscape_left";
        case UIDeviceOrientationLandscapeRight:     return "landscape_right";
        case UIDeviceOrientationFaceUp:             return "face_up";
        case UIDeviceOrientationFaceDown:           return "face_down";
        default:                                    return "unknown";
        }
    }

    /** Enable or disable the idle timer (screen dimming). */
    void set_idle_timer_enabled(bool enabled) {
        dispatch_main_async([=]() {
            [UIApplication sharedApplication].idleTimerDisabled = !enabled;
        });
    }

    /** Set the application icon badge number. */
    void set_badge_number(int number) {
        dispatch_main_async([=]() {
            [UIApplication sharedApplication].applicationIconBadgeNumber = number;
        });
    }

    /** Clear the application icon badge. */
    void clear_badge() {
        dispatch_main_async([=]() {
            [UIApplication sharedApplication].applicationIconBadgeNumber = 0;
        });
    }
};

// -------------------------------------------------------------------------
//  UIApplicationDelegate Implementation
// -------------------------------------------------------------------------
@implementation CppDeskAppDelegate

- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary<UIApplicationLaunchOptionsKey, id> *)launchOptions {

    IOS_LOGI("[ios] application:didFinishLaunchingWithOptions:");

    // Log what triggered the launch
    if (launchOptions) {
        NSArray *keys = launchOptions.allKeys;
        for (id key in keys) {
            IOS_LOGD("[ios]   launch option: {}", ns_to_std([key description]));
        }
    }

    if (g_lifecycle.on_did_finish_launching) {
        g_lifecycle.on_did_finish_launching();
    }

    return YES;
}

- (void)applicationWillResignActive:(UIApplication *)application {
    IOS_LOGI("[ios] applicationWillResignActive");
    if (g_lifecycle.on_will_resign_active) {
        g_lifecycle.on_will_resign_active();
    }
}

- (void)applicationDidEnterBackground:(UIApplication *)application {
    IOS_LOGI("[ios] applicationDidEnterBackground");
    if (g_lifecycle.on_did_enter_background) {
        g_lifecycle.on_did_enter_background();
    }
}

- (void)applicationWillEnterForeground:(UIApplication *)application {
    IOS_LOGI("[ios] applicationWillEnterForeground");
    if (g_lifecycle.on_will_enter_foreground) {
        g_lifecycle.on_will_enter_foreground();
    }
}

- (void)applicationDidBecomeActive:(UIApplication *)application {
    IOS_LOGI("[ios] applicationDidBecomeActive");
    if (g_lifecycle.on_did_become_active) {
        g_lifecycle.on_did_become_active();
    }
}

- (void)applicationWillTerminate:(UIApplication *)application {
    IOS_LOGI("[ios] applicationWillTerminate");
    if (g_lifecycle.on_will_terminate) {
        g_lifecycle.on_will_terminate();
    }
}

- (void)application:(UIApplication *)application
    didRegisterForRemoteNotificationsWithDeviceToken:(NSData *)deviceToken {

    NSString *token = [deviceToken description];
    token = [token stringByReplacingOccurrencesOfString:@"<" withString:@""];
    token = [token stringByReplacingOccurrencesOfString:@">" withString:@""];
    token = [token stringByReplacingOccurrencesOfString:@" " withString:@""];

    IOS_LOGI("[ios] Remote notification token: {}", ns_to_std(token));

    if (g_lifecycle.on_did_register_for_remote_notifications) {
        g_lifecycle.on_did_register_for_remote_notifications(
            @{@"token": token ?: @""});
    }
}

- (void)application:(UIApplication *)application
    didFailToRegisterForRemoteNotificationsWithError:(NSError *)error {

    IOS_LOGE("[ios] Push registration failed: {}",
              ns_to_std(error.localizedDescription));

    if (g_lifecycle.on_did_fail_to_register_for_remote_notifications) {
        g_lifecycle.on_did_fail_to_register_for_remote_notifications(error);
    }
}

- (void)application:(UIApplication *)application
    didReceiveRemoteNotification:(NSDictionary *)userInfo
    fetchCompletionHandler:(void (^)(UIBackgroundFetchResult))completionHandler {

    IOS_LOGI("[ios] Received remote notification (background fetch)");

    if (g_lifecycle.on_did_receive_remote_notification) {
        g_lifecycle.on_did_receive_remote_notification(userInfo, completionHandler);
    } else {
        completionHandler(UIBackgroundFetchResultNoData);
    }
}

- (BOOL)application:(UIApplication *)app
            openURL:(NSURL *)url
            options:(NSDictionary<UIApplicationOpenURLOptionsKey, id> *)options {

    IOS_LOGI("[ios] openURL: {}", ns_to_std(url.absoluteString));

    if (g_lifecycle.on_open_url) {
        return g_lifecycle.on_open_url(url, options) ? YES : NO;
    }
    return NO;
}

// --- UISceneDelegate methods (Scene-based lifecycle, iOS 13+) ---
- (void)scene:(UIScene *)scene
    willConnectToSession:(UISceneSession *)session
    options:(UISceneConnectionOptions *)connectionOptions API_AVAILABLE(ios(13.0)) {

    IOS_LOGI("[ios] scene:willConnectToSession:");

    if (g_lifecycle.on_scene_will_connect) {
        g_lifecycle.on_scene_will_connect(scene, session, connectionOptions);
    }
}

- (void)sceneDidDisconnect:(UIScene *)scene API_AVAILABLE(ios(13.0)) {
    IOS_LOGI("[ios] sceneDidDisconnect");
    if (g_lifecycle.on_scene_did_disconnect) {
        g_lifecycle.on_scene_did_disconnect(scene);
    }
}

- (void)sceneDidBecomeActive:(UIScene *)scene API_AVAILABLE(ios(13.0)) {
    IOS_LOGI("[ios] sceneDidBecomeActive");
    if (g_lifecycle.on_scene_did_become_active) {
        g_lifecycle.on_scene_did_become_active(scene);
    }
}

- (void)sceneWillResignActive:(UIScene *)scene API_AVAILABLE(ios(13.0)) {
    IOS_LOGI("[ios] sceneWillResignActive");
    if (g_lifecycle.on_scene_will_resign_active) {
        g_lifecycle.on_scene_will_resign_active(scene);
    }
}

- (void)sceneWillEnterForeground:(UIScene *)scene API_AVAILABLE(ios(13.0)) {
    IOS_LOGI("[ios] sceneWillEnterForeground");
    if (g_lifecycle.on_scene_will_enter_foreground) {
        g_lifecycle.on_scene_will_enter_foreground(scene);
    }
}

- (void)sceneDidEnterBackground:(UIScene *)scene API_AVAILABLE(ios(13.0)) {
    IOS_LOGI("[ios] sceneDidEnterBackground");
    if (g_lifecycle.on_scene_did_enter_background) {
        g_lifecycle.on_scene_did_enter_background(scene);
    }
}

@end  // CppDeskAppDelegate

// ===========================================================================
//  SECTION 5 — NETWORK EXTENSION (NEPacketTunnelProvider)
// ===========================================================================
//
//  NEPacketTunnelProvider is the core class for implementing a VPN-like
//  tunnel on iOS via the Network Extension framework.  It gives us raw
//  IP packet access, allowing us to route traffic through the cppdesk
//  connection.
//
//  This code lives in a Network Extension target (separate process).

/**
 * @interface CppDeskPacketTunnelProvider : NEPacketTunnelProvider
 *
 * Custom packet tunnel provider that bridges raw IP packets into
 * the cppdesk network layer.
 */
@interface CppDeskPacketTunnelProvider : NEPacketTunnelProvider
@end

// --- C++ tunnel state ---
struct PacketTunnelState {
    std::atomic<bool> connected{false};
    std::atomic<int64_t> bytes_sent{0};
    std::atomic<int64_t> bytes_received{0};
    std::atomic<int64_t> packets_sent{0};
    std::atomic<int64_t> packets_received{0};
    std::string tunnel_remote_address;
    std::string tunnel_local_address;

    // Callbacks
    std::function<void(NEPacketTunnelFlow *)> on_flow_ready;
    std::function<void(std::vector<uint8_t>, int protocol_family)> on_packet_received;
    std::function<void(NSError *)> on_error;
};

static PacketTunnelState g_tunnel_state;

@implementation CppDeskPacketTunnelProvider

/**
 * Called when the system wants to start the VPN tunnel.
 * We configure the tunnel network settings and set up a virtual
 * interface (utun) through which packets are routed.
 */
- (void)startTunnelWithOptions:(NSDictionary<NSString *, NSObject *> *)options
             completionHandler:(void (^)(NSError * _Nullable))completionHandler {

    IOS_LOGI("[ios] NEPacketTunnelProvider startTunnelWithOptions:");

    if (options) {
        IOS_LOGD("[ios] Tunnel options: {}", ns_to_std(options.description ?: @""));
    }

    // --- Configure tunnel network settings ---
    NEPacketTunnelNetworkSettings *settings =
        [[NEPacketTunnelNetworkSettings alloc] initWithTunnelRemoteAddress:@"10.8.0.1"];

    // IPv4 settings: assign a /24 subnet
    NEIPv4Settings *ipv4 = [[NEIPv4Settings alloc]
        initWithAddresses:@[@"10.8.0.2"]
        subnetMasks:@[@"255.255.255.0"]];

    // Add a route to capture all traffic (0.0.0.0/0 via tunnel)
    NEIPv4Route *defaultRoute = [[NEIPv4Route alloc]
        initWithDestinationAddress:@"0.0.0.0"
        subnetMask:@"0.0.0.0"];
    ipv4.includedRoutes = @[defaultRoute];
    ipv4.excludedRoutes = @[];

    settings.IPv4Settings = ipv4;

    // DNS settings
    NEDNSSettings *dns = [[NEDNSSettings alloc] initWithServers:@[@"8.8.8.8", @"1.1.1.1"]];
    dns.matchDomains = @[@""]; // match all
    settings.DNSSettings = dns;

    // MTU
    settings.MTU = @(1400);

    g_tunnel_state.tunnel_remote_address = "10.8.0.1";
    g_tunnel_state.tunnel_local_address  = "10.8.0.2";

    // Apply settings
    __weak typeof(self) weakSelf = self;
    [self setTunnelNetworkSettings:settings completionHandler:^(NSError * _Nullable error) {
        if (error) {
            IOS_LOGE("[ios] Failed to set tunnel network settings: {}",
                      ns_to_std(error.localizedDescription));
            completionHandler(error);
            return;
        }

        IOS_LOGI("[ios] Tunnel network settings applied successfully");
        g_tunnel_state.connected.store(true);

        // Start reading packets from the tunnel flow
        [weakSelf startReadingPackets];

        completionHandler(nil);
    }];
}

/**
 * Called when the system wants to stop the VPN tunnel.
 */
- (void)stopTunnelWithReason:(NEProviderStopReason)reason
           completionHandler:(void (^)(void))completionHandler {

    std::string reason_str;
    switch (reason) {
    case NEProviderStopReasonNone:             reason_str = "none"; break;
    case NEProviderStopReasonUserInitiated:    reason_str = "user_initiated"; break;
    case NEProviderStopReasonProviderFailed:   reason_str = "provider_failed"; break;
    case NEProviderStopReasonNoNetworkAvailable: reason_str = "no_network"; break;
    case NEProviderStopReasonUnrecoverableNetworkChange: reason_str = "unrecoverable_change"; break;
    case NEProviderStopReasonProviderDisabled: reason_str = "disabled"; break;
    case NEProviderStopReasonAuthenticationCanceled: reason_str = "auth_canceled"; break;
    case NEProviderStopReasonConfigurationFailed: reason_str = "config_failed"; break;
    case NEProviderStopReasonIdleTimeout:      reason_str = "idle_timeout"; break;
    case NEProviderStopReasonConfigurationDisabled: reason_str = "config_disabled"; break;
    case NEProviderStopReasonConfigurationRemoved: reason_str = "config_removed"; break;
    case NEProviderStopReasonSuperceded:       reason_str = "superceded"; break;
    case NEProviderStopReasonUserLogout:       reason_str = "user_logout"; break;
    case NEProviderStopReasonUserSwitch:       reason_str = "user_switch"; break;
    case NEProviderStopReasonConnectionFailure: reason_str = "connection_failure"; break;
    default: reason_str = "unknown"; break;
    }

    IOS_LOGI("[ios] stopTunnelWithReason: {}", reason_str);
    g_tunnel_state.connected.store(false);

    completionHandler();
}

/**
 * Handle app message from the containing app (IPC).
 */
- (void)handleAppMessage:(NSData *)messageData
       completionHandler:(void (^)(NSData * _Nullable))completionHandler {

    IOS_LOGD("[ios] handleAppMessage: {} bytes", (unsigned long)messageData.length);

    // Echo back for demo; in reality: parse the message and act
    completionHandler(messageData);
}

/**
 * Continuously read packets from the tunnel flow.
 */
- (void)startReadingPackets {
    NEPacketTunnelFlow *flow = self.packetFlow;

    [flow readPacketObjectsWithCompletionHandler:^(NSArray<NEPacket *> *packets) {
        if (!g_tunnel_state.connected.load()) return;

        for (NEPacket *packet in packets) {
            NSData *data = packet.data;
            g_tunnel_state.packets_received.fetch_add(1);
            g_tunnel_state.bytes_received.fetch_add(data.length);

            // Pass to C++ layer
            if (g_tunnel_state.on_packet_received) {
                std::vector<uint8_t> vec((const uint8_t *)data.bytes,
                                          (const uint8_t *)data.bytes + data.length);
                g_tunnel_state.on_packet_received(
                    std::move(vec),
                    (int)packet.protocolFamily);
            }
        }

        // Continue reading
        [self startReadingPackets];
    }];
}

/**
 * Write packets to the tunnel flow.
 */
- (void)writePackets:(NSArray<NEPacket *> *)packets {
    NEPacketTunnelFlow *flow = self.packetFlow;

    [flow writePacketObjects:packets completionHandler:^(NSError * _Nullable error) {
        if (error) {
            IOS_LOGE("[ios] writePacketObjects error: {}",
                      ns_to_std(error.localizedDescription));
            if (g_tunnel_state.on_error) {
                g_tunnel_state.on_error(error);
            }
            return;
        }

        for (NEPacket *pkt in packets) {
            g_tunnel_state.packets_sent.fetch_add(1);
            g_tunnel_state.bytes_sent.fetch_add(pkt.data.length);
        }
    }];
}

@end  // CppDeskPacketTunnelProvider

// --- C++ wrapper for managing the tunnel from the main app ---
struct PacketTunnelProvider {
    std::string tunnel_bundle_id;

    /** Start the VPN tunnel from the main app. */
    bool start_tunnel(std::string &error_out) {
        NETunnelProviderManager *manager = [self _find_or_create_manager];
        if (!manager) {
            error_out = "Could not create NETunnelProviderManager";
            return false;
        }

        manager.enabled = YES;

        NSError *saveError = nil;
        [manager saveToPreferencesWithCompletionHandler:^(NSError * _Nullable error) {
            if (error) {
                IOS_LOGE("[ios] saveToPreferences error: {}",
                          ns_to_std(error.localizedDescription));
                return;
            }
            // Start the tunnel
            [manager.connection startVPNTunnelAndReturnError:&saveError];
            if (saveError) {
                IOS_LOGE("[ios] startVPNTunnel error: {}",
                          ns_to_std(saveError.localizedDescription));
            } else {
                IOS_LOGI("[ios] VPN tunnel started");
            }
        }];

        return true;
    }

    /** Stop the VPN tunnel. */
    void stop_tunnel() {
        NETunnelProviderManager *manager = [self _find_existing_manager];
        if (manager) {
            [manager.connection stopVPNTunnel];
            IOS_LOGI("[ios] VPN tunnel stopped");
        }
    }

    /** Get the tunnel connection status. */
    std::string connection_status() const {
        NETunnelProviderManager *manager = [self _find_existing_manager];
        if (!manager) return "not_configured";

        switch (manager.connection.status) {
        case NEVPNStatusInvalid:     return "invalid";
        case NEVPNStatusDisconnected: return "disconnected";
        case NEVPNStatusConnecting:  return "connecting";
        case NEVPNStatusConnected:   return "connected";
        case NEVPNStatusReasserting: return "reasserting";
        case NEVPNStatusDisconnecting: return "disconnecting";
        default: return "unknown";
        }
    }

private:
    NETunnelProviderManager *_find_existing_manager() const {
        // In practice, iterate [NETunnelProviderManager loadAllFromPreferences]
        // This is a simplified placeholder.
        return nil;
    }

    NETunnelProviderManager *_find_or_create_manager() {
        NETunnelProviderManager *manager = [[NETunnelProviderManager alloc] init];
        NETunnelProviderProtocol *proto = [[NETunnelProviderProtocol alloc] init];
        proto.providerBundleIdentifier = std_to_ns(tunnel_bundle_id);
        proto.serverAddress = @"cppdesk.local";
        manager.protocolConfiguration = proto;
        manager.localizedDescription = @"cppdesk Tunnel";
        return manager;
    }
};

// ===========================================================================
//  SECTION 6 — LOCAL NOTIFICATION SCHEDULING
// ===========================================================================
//
//  UNUserNotificationCenter (iOS 10+) for scheduling local notifications.
//  Supports time-based, calendar-based, and location-based triggers.

struct NotificationScheduler {

    /** Request notification authorization. */
    void request_authorization(std::function<void(bool granted, std::string error)> callback) {
        UNUserNotificationCenter *center =
            [UNUserNotificationCenter currentNotificationCenter];

        UNAuthorizationOptions options =
            UNAuthorizationOptionAlert |
            UNAuthorizationOptionSound |
            UNAuthorizationOptionBadge |
            UNAuthorizationOptionCriticalAlert; // iOS 12+

        [center requestAuthorizationWithOptions:options
                              completionHandler:^(BOOL granted, NSError * _Nullable error) {
            std::string err_str = error ? ns_to_std(error.localizedDescription) : "";
            IOS_LOGI("[ios] Notification authorization: {} (err={})",
                      granted ? "granted" : "denied", err_str);
            if (callback) callback(granted, err_str);
        }];
    }

    /** Check current notification settings. */
    void get_authorization_status(std::function<void(std::string status)> callback) {
        UNUserNotificationCenter *center =
            [UNUserNotificationCenter currentNotificationCenter];

        [center getNotificationSettingsWithCompletionHandler:^(
            UNNotificationSettings *settings) {

            std::string status;
            switch (settings.authorizationStatus) {
            case UNAuthorizationStatusNotDetermined: status = "not_determined"; break;
            case UNAuthorizationStatusDenied:        status = "denied"; break;
            case UNAuthorizationStatusAuthorized:    status = "authorized"; break;
            case UNAuthorizationStatusProvisional:   status = "provisional"; break;
            default: status = "unknown"; break;
            }
            if (callback) callback(status);
        }];
    }

    /** Schedule a time-interval notification. */
    std::string schedule_time_interval(const std::string &identifier,
                                        const std::string &title,
                                        const std::string &body,
                                        double seconds_from_now,
                                        bool repeats = false) {
        UNMutableNotificationContent *content = [[UNMutableNotificationContent alloc] init];
        content.title = std_to_ns(title);
        content.body  = std_to_ns(body);
        content.sound = [UNNotificationSound defaultSound];
        content.badge = @(1);

        // Attach category for action buttons
        content.categoryIdentifier = @"CPPDESK_NOTIFY";

        UNTimeIntervalNotificationTrigger *trigger = [UNTimeIntervalNotificationTrigger
            triggerWithTimeInterval:seconds_from_now repeats:repeats];

        NSString *reqId = std_to_ns(identifier);
        UNNotificationRequest *request = [UNNotificationRequest
            requestWithIdentifier:reqId content:content trigger:trigger];

        UNUserNotificationCenter *center =
            [UNUserNotificationCenter currentNotificationCenter];

        [center addNotificationRequest:request withCompletionHandler:^(
            NSError * _Nullable error) {
            if (error) {
                IOS_LOGE("[ios] Schedule notification failed: {}",
                          ns_to_std(error.localizedDescription));
            } else {
                IOS_LOGI("[ios] Notification scheduled: {} (in {}s)",
                          identifier, seconds_from_now);
            }
        }];

        return identifier;
    }

    /** Schedule a calendar-based notification. */
    std::string schedule_calendar(const std::string &identifier,
                                   const std::string &title,
                                   const std::string &body,
                                   int year, int month, int day,
                                   int hour, int minute) {
        UNMutableNotificationContent *content = [[UNMutableNotificationContent alloc] init];
        content.title = std_to_ns(title);
        content.body  = std_to_ns(body);
        content.sound = [UNNotificationSound defaultSound];

        NSDateComponents *dateComponents = [[NSDateComponents alloc] init];
        dateComponents.year   = year;
        dateComponents.month  = month;
        dateComponents.day    = day;
        dateComponents.hour   = hour;
        dateComponents.minute = minute;

        UNCalendarNotificationTrigger *trigger = [UNCalendarNotificationTrigger
            triggerWithDateMatchingComponents:dateComponents repeats:NO];

        NSString *reqId = std_to_ns(identifier);
        UNNotificationRequest *request = [UNNotificationRequest
            requestWithIdentifier:reqId content:content trigger:trigger];

        UNUserNotificationCenter *center =
            [UNUserNotificationCenter currentNotificationCenter];

        [center addNotificationRequest:request withCompletionHandler:^(
            NSError * _Nullable error) {
            if (error) {
                IOS_LOGE("[ios] Calendar notification failed: {}",
                          ns_to_std(error.localizedDescription));
            } else {
                IOS_LOGI("[ios] Calendar notification scheduled: {}", identifier);
            }
        }];

        return identifier;
    }

    /** Remove a pending notification. */
    void remove_pending(const std::string &identifier) {
        UNUserNotificationCenter *center =
            [UNUserNotificationCenter currentNotificationCenter];

        [center removePendingNotificationRequestsWithIdentifiers:
            @[std_to_ns(identifier)]];
        IOS_LOGD("[ios] Removed pending notification: {}", identifier);
    }

    /** Remove all pending notifications. */
    void remove_all_pending() {
        UNUserNotificationCenter *center =
            [UNUserNotificationCenter currentNotificationCenter];
        [center removeAllPendingNotificationRequests];
        IOS_LOGI("[ios] All pending notifications removed");
    }

    /** Remove all delivered notifications (from Notification Center). */
    void remove_all_delivered() {
        UNUserNotificationCenter *center =
            [UNUserNotificationCenter currentNotificationCenter];
        [center removeAllDeliveredNotifications];
        IOS_LOGI("[ios] All delivered notifications removed");
    }

    /** Get all pending notification requests. */
    void get_pending(std::function<void(std::vector<std::string>)> callback) {
        UNUserNotificationCenter *center =
            [UNUserNotificationCenter currentNotificationCenter];

        [center getPendingNotificationRequestsWithCompletionHandler:^(
            NSArray<UNNotificationRequest *> *requests) {

            std::vector<std::string> ids;
            for (UNNotificationRequest *req in requests) {
                ids.push_back(ns_to_std(req.identifier));
            }
            if (callback) callback(ids);
        }];
    }

    /** Register notification categories (action buttons). */
    void register_categories() {
        UNNotificationAction *viewAction = [UNNotificationAction
            actionWithIdentifier:@"VIEW_ACTION"
            title:@"View"
            options:UNNotificationActionOptionForeground];

        UNNotificationAction *dismissAction = [UNNotificationAction
            actionWithIdentifier:@"DISMISS_ACTION"
            title:@"Dismiss"
            options:UNNotificationActionOptionDestructive];

        UNNotificationCategory *category = [UNNotificationCategory
            categoryWithIdentifier:@"CPPDESK_NOTIFY"
            actions:@[viewAction, dismissAction]
            intentIdentifiers:@[]
            options:UNNotificationCategoryOptionCustomDismissAction];

        UNUserNotificationCenter *center =
            [UNUserNotificationCenter currentNotificationCenter];

        [center setNotificationCategories:[NSSet setWithObject:category]];
        IOS_LOGI("[ios] Notification categories registered");
    }
};

// ===========================================================================
//  SECTION 7 — BACKGROUND TASK MANAGEMENT
// ===========================================================================
//
//  iOS provides limited background execution time via
//  beginBackgroundTaskWithExpirationHandler.
//
//  iOS 13+ also introduced BGTaskScheduler for more flexible background
//  work (BGAppRefreshTask, BGProcessingTask).

struct BackgroundTaskManager {
    UIBackgroundTaskIdentifier current_task = UIBackgroundTaskInvalid;
    std::string task_name;
    std::atomic<bool> task_running{false};

    /**
     * Begin a background task.  The system grants a finite amount of
     * time (typically ~30 seconds on modern iOS, but can vary).
     *
     * Returns the task identifier; store it and call end_task() when done.
     */
    bool begin_task(const std::string &name) {
        if (task_running.load()) {
            IOS_LOGW("[ios] Background task already running: {}", task_name);
            return false;
        }

        task_name = name;

        dispatch_main_sync([&]() {
            __block UIBackgroundTaskIdentifier taskId;

            taskId = [[UIApplication sharedApplication]
                beginBackgroundTaskWithName:std_to_ns(name)
                expirationHandler:^{
                    // The system is about to kill us if we don't end the task
                    IOS_LOGW("[ios] Background task '{}' expiring! "
                              "Time remaining: {:.1f}s",
                              self->task_name,
                              [[UIApplication sharedApplication] backgroundTimeRemaining]);

                    // Clean up immediately
                    [[UIApplication sharedApplication] endBackgroundTask:taskId];
                    self->task_running.store(false);
                    self->current_task = UIBackgroundTaskInvalid;
                }];

            self->current_task = taskId;
            self->task_running.store(taskId != UIBackgroundTaskInvalid);
        });

        if (task_running.load()) {
            IOS_LOGI("[ios] Background task '{}' started (id={})",
                      task_name, (unsigned long)current_task);
        } else {
            IOS_LOGE("[ios] Failed to begin background task '{}'", task_name);
        }

        return task_running.load();
    }

    /** End a previously started background task. */
    void end_task() {
        if (!task_running.load()) return;

        dispatch_main_async([&]() {
            if (self->current_task != UIBackgroundTaskInvalid) {
                [[UIApplication sharedApplication] endBackgroundTask:self->current_task];
                IOS_LOGI("[ios] Background task '{}' ended (id={})",
                          self->task_name, (unsigned long)self->current_task);
                self->current_task = UIBackgroundTaskInvalid;
                self->task_running.store(false);
            }
        });
    }

    /** Get remaining background time in seconds. */
    double time_remaining() const {
        return [[UIApplication sharedApplication] backgroundTimeRemaining];
    }

    /** Check if this device supports BGTaskScheduler (iOS 13+). */
    bool supports_bg_task_scheduler() const {
        if (@available(iOS 13.0, *)) {
            return true;
        }
        return false;
    }

#if __has_include(<BackgroundTasks/BackgroundTasks.h>)
    /**
     * Register a BGAppRefreshTask for periodic background fetch.
     * Requires the "background fetch" capability and Info.plist entry.
     */
    void register_app_refresh_task(const std::string &identifier) {
        if (@available(iOS 13.0, *)) {
            [[BGTaskScheduler sharedScheduler]
                registerForTaskWithIdentifier:std_to_ns(identifier)
                usingQueue:nil
                launchHandler:^(__kindof BGTask *task) {

                    IOS_LOGI("[ios] BGAppRefreshTask launched: {}",
                              ns_to_std(task.identifier));

                    // Set expiration handler
                    task.expirationHandler = ^{
                        IOS_LOGW("[ios] BGAppRefreshTask '{}' expired",
                                  ns_to_std(task.identifier));
                        [task setTaskCompletedWithSuccess:NO];
                    };

                    // Do background work...
                    // When done:
                    [task setTaskCompletedWithSuccess:YES];
                }];
            IOS_LOGI("[ios] BGAppRefreshTask registered: {}", identifier);
        }
    }

    /**
     * Submit a BGAppRefreshTask request to be scheduled by the system.
     */
    void submit_app_refresh_request(const std::string &identifier,
                                     double earliest_begin_date_seconds = 60.0) {
        if (@available(iOS 13.0, *)) {
            BGAppRefreshTaskRequest *request = [[BGAppRefreshTaskRequest alloc]
                initWithIdentifier:std_to_ns(identifier)];
            request.earliestBeginDate = [NSDate
                dateWithTimeIntervalSinceNow:earliest_begin_date_seconds];

            NSError *error = nil;
            [[BGTaskScheduler sharedScheduler] submitTaskRequest:request error:&error];
            if (error) {
                IOS_LOGE("[ios] Submit BGAppRefreshTask failed: {}",
                          ns_to_std(error.localizedDescription));
            } else {
                IOS_LOGI("[ios] BGAppRefreshTask submitted: {}", identifier);
            }
        }
    }
#endif
};

// ===========================================================================
//  SECTION 8 — FILE SHARING via UIDocumentPickerViewController
// ===========================================================================
//
//  UIDocumentPickerViewController allows users to pick files from
//  iCloud Drive, third-party file providers, and local storage.
//  We wrap it in a C++ interface for the cppdesk engine.

struct DocumentPicker {
    // --- State ---
    struct PickerState {
        std::function<void(std::vector<std::string> urls)> on_picked;
        std::function<void(std::string error)>              on_cancel;
        bool allow_multiple = false;
        std::vector<std::string> allowed_utis;
    };

    PickerState state;

    /**
     * Present the document picker for importing files.
     *
     * @param view_controller  The presenting view controller (usually root VC).
     * @param allow_multiple   Whether to allow multiple selection.
     * @param utis             Array of UTType identifiers (e.g., "public.data").
     * @param on_picked        Called with selected file URLs.
     * @param on_cancel        Called if user cancels.
     */
    void present_import_picker(UIViewController *view_controller,
                                bool allow_multiple,
                                const std::vector<std::string> &utis,
                                std::function<void(std::vector<std::string>)> on_picked,
                                std::function<void(std::string)> on_cancel) {

        state.on_picked = std::move(on_picked);
        state.on_cancel  = std::move(on_cancel);
        state.allow_multiple = allow_multiple;
        state.allowed_utis = utis;

        NSMutableArray<UTType *> *types = [NSMutableArray array];
        if (utis.empty()) {
            // Default: all data types
            [types addObject:UTTypeData];
        } else {
            for (const auto &uti : utis) {
                UTType *t = [UTType typeWithIdentifier:std_to_ns(uti)];
                if (t) [types addObject:t];
            }
        }

        dispatch_main_async([=]() {
            UIDocumentPickerViewController *picker;

            if (@available(iOS 14.0, *)) {
                picker = [[UIDocumentPickerViewController alloc]
                    initForOpeningContentTypes:types asCopy:YES];
            } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                NSMutableArray<NSString *> *typeStrings = [NSMutableArray array];
                for (const auto &uti : utis) {
                    [typeStrings addObject:std_to_ns(uti)];
                }
                picker = [[UIDocumentPickerViewController alloc]
                    initWithDocumentTypes:typeStrings inMode:UIDocumentPickerModeImport];
#pragma clang diagnostic pop
            }

            picker.allowsMultipleSelection = allow_multiple ? YES : NO;
            picker.delegate = (id<UIDocumentPickerDelegate>)self->_delegate_bridge();

            [view_controller presentViewController:picker animated:YES completion:nil];
            IOS_LOGI("[ios] Document picker presented (multiple={})",
                      allow_multiple);
        });
    }

    /**
     * Present the document picker for exporting a file.
     */
    void present_export_picker(UIViewController *view_controller,
                                const std::string &source_url,
                                std::function<void(std::string)> on_complete) {

        NSURL *url = [NSURL fileURLWithPath:std_to_ns(source_url)];

        dispatch_main_async([=]() {
            UIDocumentPickerViewController *picker;

            if (@available(iOS 14.0, *)) {
                picker = [[UIDocumentPickerViewController alloc]
                    initForExportingURLs:@[url] asCopy:YES];
            } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                picker = [[UIDocumentPickerViewController alloc]
                    initWithURL:url inMode:UIDocumentPickerModeExportToService];
#pragma clang diagnostic pop
            }

            picker.delegate = (id<UIDocumentPickerDelegate>)self->_delegate_bridge();
            [view_controller presentViewController:picker animated:YES completion:nil];
            IOS_LOGI("[ios] Export picker presented for: {}", source_url);
        });

        state.on_picked = [on_complete](std::vector<std::string> urls) {
            if (!urls.empty() && on_complete) on_complete(urls[0]);
        };
    }

private:
    /**
     * Returns a bridge object that implements UIDocumentPickerDelegate
     * and forwards events into the C++ PickerState.
     *
     * We use an Objective-C helper class (defined inline below).
     */
    id _delegate_bridge();

    friend struct DocumentPickerDelegate;
};

/**
 * @interface DocumentPickerDelegate : NSObject <UIDocumentPickerDelegate>
 * Internal delegate that bridges UIDocumentPickerDelegate callbacks to C++.
 */
@interface DocumentPickerDelegate : NSObject <UIDocumentPickerDelegate>
@property (nonatomic, assign) DocumentPicker *cpp_picker;
@end

@implementation DocumentPickerDelegate

- (void)documentPicker:(UIDocumentPickerViewController *)controller
    didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {

    IOS_LOGI("[ios] DocumentPicker: picked {} file(s)", (unsigned long)urls.count);

    if (_cpp_picker && _cpp_picker->state.on_picked) {
        std::vector<std::string> paths;
        for (NSURL *url in urls) {
            paths.push_back(ns_to_std(url.path));
            IOS_LOGD("[ios]   -> {}", ns_to_std(url.path));
        }
        _cpp_picker->state.on_picked(paths);
    }
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller {
    IOS_LOGI("[ios] DocumentPicker: cancelled");
    if (_cpp_picker && _cpp_picker->state.on_cancel) {
        _cpp_picker->state.on_cancel("User cancelled");
    }
}

@end

// --- Factory method ---
id DocumentPicker::_delegate_bridge() {
    DocumentPickerDelegate *del = [[DocumentPickerDelegate alloc] init];
    del.cpp_picker = this;
    return del;
}

// ===========================================================================
//  SECTION 9 — CLIPBOARD ACCESS (UIPasteboard)
// ===========================================================================
//
//  UIPasteboard (general pasteboard) provides access to the system
//  clipboard.  iOS 14+ introduced a privacy notification when apps
//  read the clipboard.

struct ClipboardManager {

    /** Check if the general pasteboard has a string. */
    bool has_string() const {
        return [[UIPasteboard generalPasteboard] hasStrings];
    }

    /** Check if the general pasteboard has an image. */
    bool has_image() const {
        return [[UIPasteboard generalPasteboard] hasImages];
    }

    /** Check if the general pasteboard has a URL. */
    bool has_url() const {
        return [[UIPasteboard generalPasteboard] hasURLs];
    }

    /** Get the string from the general pasteboard. */
    std::optional<std::string> get_string() const {
        NSString *s = [UIPasteboard generalPasteboard].string;
        if (!s) return std::nullopt;
        return ns_to_std(s);
    }

    /** Get the URL from the general pasteboard. */
    std::optional<std::string> get_url() const {
        NSURL *url = [UIPasteboard generalPasteboard].URL;
        if (!url) return std::nullopt;
        return ns_to_std(url.absoluteString);
    }

    /** Get an image from the general pasteboard (as PNG data). */
    std::optional<std::vector<uint8_t>> get_image_png() const {
        UIImage *img = [UIPasteboard generalPasteboard].image;
        if (!img) return std::nullopt;

        NSData *pngData = UIImagePNGRepresentation(img);
        if (!pngData) return std::nullopt;

        return std::vector<uint8_t>((const uint8_t *)pngData.bytes,
                                     (const uint8_t *)pngData.bytes + pngData.length);
    }

    /** Get an image from the general pasteboard (as JPEG data). */
    std::optional<std::vector<uint8_t>> get_image_jpeg(float quality = 0.9f) const {
        UIImage *img = [UIPasteboard generalPasteboard].image;
        if (!img) return std::nullopt;

        NSData *jpegData = UIImageJPEGRepresentation(img, quality);
        if (!jpegData) return std::nullopt;

        return std::vector<uint8_t>((const uint8_t *)jpegData.bytes,
                                     (const uint8_t *)jpegData.bytes + jpegData.length);
    }

    /** Set a string on the general pasteboard. */
    void set_string(const std::string &text) {
        [UIPasteboard generalPasteboard].string = std_to_ns(text);
        IOS_LOGD("[ios] Clipboard set: {} chars", text.size());
    }

    /** Set a URL on the general pasteboard. */
    void set_url(const std::string &url) {
        [UIPasteboard generalPasteboard].URL = [NSURL URLWithString:std_to_ns(url)];
        IOS_LOGD("[ios] Clipboard set URL: {}", url);
    }

    /** Set an image on the general pasteboard from raw data. */
    void set_image_from_data(const std::vector<uint8_t> &data) {
        NSData *nsdata = [NSData dataWithBytes:data.data() length:data.size()];
        UIImage *img = [UIImage imageWithData:nsdata];
        if (img) {
            [UIPasteboard generalPasteboard].image = img;
            IOS_LOGD("[ios] Clipboard set image: {} bytes", data.size());
        } else {
            IOS_LOGE("[ios] Failed to decode image data for clipboard");
        }
    }

    /** Set multiple items on the pasteboard. */
    void set_items(const std::vector<std::pair<std::string, std::string>> &items) {
        NSMutableArray<NSDictionary *> *nsItems = [NSMutableArray array];

        for (const auto &[key, value] : items) {
            NSDictionary *dict = @{
                std_to_ns(key) : std_to_ns(value)
            };
            [nsItems addObject:dict];
        }

        [UIPasteboard generalPasteboard].items = nsItems;
        IOS_LOGD("[ios] Clipboard set {} items", items.size());
    }

    /** Get the number of items on the pasteboard. */
    int item_count() const {
        return (int)[UIPasteboard generalPasteboard].numberOfItems;
    }

    /** Get the change count (increments each time pasteboard changes). */
    int change_count() const {
        return (int)[UIPasteboard generalPasteboard].changeCount;
    }

    /** Detect if content has changed since a previous count. */
    bool has_changed_since(int previous_count) const {
        return change_count() != previous_count;
    }

    /** Create a named pasteboard for app-private sharing. */
    static std::string create_named(const std::string &name) {
        UIPasteboard *pb = [UIPasteboard pasteboardWithName:std_to_ns(name) create:YES];
        pb.persistent = YES;
        IOS_LOGI("[ios] Named pasteboard created: {}", name);
        return name;
    }

    /** Remove a named pasteboard. */
    static void remove_named(const std::string &name) {
        [UIPasteboard removePasteboardWithName:std_to_ns(name)];
        IOS_LOGI("[ios] Named pasteboard removed: {}", name);
    }
};

// ===========================================================================
//  SECTION 10 — BATTERY AND DEVICE INFO
// ===========================================================================
//
//  UIDevice provides basic device information, battery level/state,
//  and proximity sensor.  sysctl gives deeper hardware details.

struct DeviceInfo {
    // --- Device Identification ---

    std::string device_name() const {
        return ns_to_std([UIDevice currentDevice].name);
    }

    std::string system_name() const {
        return ns_to_std([UIDevice currentDevice].systemName);
    }

    std::string system_version() const {
        return ns_to_std([UIDevice currentDevice].systemVersion);
    }

    std::string model() const {
        return ns_to_std([UIDevice currentDevice].model);
    }

    std::string localized_model() const {
        return ns_to_std([UIDevice currentDevice].localizedModel);
    }

    /**
     * Get the hardware model identifier (e.g., "iPhone14,2").
     * Uses sysctl to retrieve hw.machine.
     */
    std::string hardware_model() const {
        size_t size = 0;
        sysctlbyname("hw.machine", nullptr, &size, nullptr, 0);
        if (size == 0) return "unknown";

        std::vector<char> buf(size);
        sysctlbyname("hw.machine", buf.data(), &size, nullptr, 0);
        return std::string(buf.data());
    }

    /**
     * Get a human-readable device type string.
     */
    std::string device_type_string() const {
        UIUserInterfaceIdiom idiom = [UIDevice currentDevice].userInterfaceIdiom;
        switch (idiom) {
        case UIUserInterfaceIdiomPhone:    return "iphone";
        case UIUserInterfaceIdiomPad:      return "ipad";
        case UIUserInterfaceIdiomTV:       return "appletv";
        case UIUserInterfaceIdiomCarPlay:  return "carplay";
        case UIUserInterfaceIdiomMac:      return "mac_catalyst";
        default:                           return "unknown";
        }
    }

    // --- Device Capabilities ---

    bool is_multitasking_supported() const {
        return [UIDevice currentDevice].isMultitaskingSupported;
    }

    // --- Battery ---

    /** Enable battery monitoring (must be called before reading battery state). */
    void enable_battery_monitoring() {
        [UIDevice currentDevice].batteryMonitoringEnabled = YES;
    }

    /** Disable battery monitoring to save power. */
    void disable_battery_monitoring() {
        [UIDevice currentDevice].batteryMonitoringEnabled = NO;
    }

    /** Get battery level (0.0 to 1.0, or -1.0 if unknown). */
    float battery_level() const {
        return [UIDevice currentDevice].batteryLevel;
    }

    /** Get battery level as integer percentage (0-100, or -1 unknown). */
    int battery_percent() const {
        float level = battery_level();
        if (level < 0.0f) return -1;
        return static_cast<int>(level * 100.0f);
    }

    /** Get battery state as a string. */
    std::string battery_state_string() const {
        UIDeviceBatteryState state = [UIDevice currentDevice].batteryState;
        switch (state) {
        case UIDeviceBatteryStateUnknown:    return "unknown";
        case UIDeviceBatteryStateUnplugged:  return "unplugged";
        case UIDeviceBatteryStateCharging:   return "charging";
        case UIDeviceBatteryStateFull:       return "full";
        default:                             return "unknown";
        }
    }

    /** Check if the device is plugged in and charging. */
    bool is_charging() const {
        UIDeviceBatteryState state = [UIDevice currentDevice].batteryState;
        return state == UIDeviceBatteryStateCharging ||
               state == UIDeviceBatteryStateFull;
    }

    // --- Proximity Sensor ---

    void enable_proximity_monitoring() {
        [UIDevice currentDevice].proximityMonitoringEnabled = YES;
    }

    void disable_proximity_monitoring() {
        [UIDevice currentDevice].proximityMonitoringEnabled = NO;
    }

    bool proximity_state() const {
        return [UIDevice currentDevice].proximityState;
    }

    // --- Memory ---

    /** Get physical memory in bytes (via sysctl). */
    uint64_t physical_memory() const {
        int mib[2] = { CTL_HW, HW_MEMSIZE };
        uint64_t memsize = 0;
        size_t len = sizeof(memsize);
        sysctl(mib, 2, &memsize, &len, nullptr, 0);
        return memsize;
    }

    /** Get physical memory in megabytes. */
    int physical_memory_mb() const {
        return static_cast<int>(physical_memory() / (1024 * 1024));
    }

    /** Get current process resident memory in bytes. */
    uint64_t process_resident_memory() const {
        struct mach_task_basic_info info;
        mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
        if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                      (task_info_t)&info, &count) != KERN_SUCCESS) {
            return 0;
        }
        return info.resident_size;
    }

    /** Get current process memory in megabytes. */
    double process_memory_mb() const {
        return static_cast<double>(process_resident_memory()) / (1024.0 * 1024.0);
    }

    // --- CPU ---

    /** Get number of CPU cores (via sysctl). */
    int cpu_core_count() const {
        int count = 0;
        size_t len = sizeof(count);
        sysctlbyname("hw.ncpu", &count, &len, nullptr, 0);
        return count;
    }

    /** Get CPU usage of current process (0.0-1.0, approximate). */
    double process_cpu_usage() const {
        thread_array_t thread_list;
        mach_msg_type_number_t thread_count;
        double total = 0.0;

        if (task_threads(mach_task_self(), &thread_list, &thread_count) != KERN_SUCCESS) {
            return -1.0;
        }

        for (mach_msg_type_number_t i = 0; i < thread_count; ++i) {
            thread_basic_info_data_t info;
            mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;

            if (thread_info(thread_list[i], THREAD_BASIC_INFO,
                            (thread_info_t)&info, &count) == KERN_SUCCESS) {
                if ((info.flags & TH_FLAGS_IDLE) == 0) {
                    total += (double)info.cpu_usage / (double)TH_USAGE_SCALE;
                }
            }
        }

        vm_deallocate(mach_task_self(), (vm_address_t)thread_list,
                      thread_count * sizeof(thread_t));
        return total;
    }

    // --- Disk ---

    /** Get free disk space in bytes. */
    uint64_t free_disk_space() const {
        NSDictionary *attrs = [[NSFileManager defaultManager]
            attributesOfFileSystemForPath:NSHomeDirectory() error:nil];
        if (!attrs) return 0;
        return [attrs[NSFileSystemFreeSize] unsignedLongLongValue];
    }

    /** Get total disk space in bytes. */
    uint64_t total_disk_space() const {
        NSDictionary *attrs = [[NSFileManager defaultManager]
            attributesOfFileSystemForPath:NSHomeDirectory() error:nil];
        if (!attrs) return 0;
        return [attrs[NSFileSystemSize] unsignedLongLongValue];
    }

    // --- Carrier Info ---

#if __has_include(<CoreTelephony/CTTelephonyNetworkInfo.h>)
    std::string carrier_name() const {
        CTTelephonyNetworkInfo *info = [[CTTelephonyNetworkInfo alloc] init];
        CTCarrier *carrier = info.subscriberCellularProvider;
        if (!carrier) return "";
        return ns_to_std(carrier.carrierName);
    }

    std::string mobile_country_code() const {
        CTTelephonyNetworkInfo *info = [[CTTelephonyNetworkInfo alloc] init];
        CTCarrier *carrier = info.subscriberCellularProvider;
        if (!carrier) return "";
        return ns_to_std(carrier.mobileCountryCode);
    }

    std::string mobile_network_code() const {
        CTTelephonyNetworkInfo *info = [[CTTelephonyNetworkInfo alloc] init];
        CTCarrier *carrier = info.subscriberCellularProvider;
        if (!carrier) return "";
        return ns_to_std(carrier.mobileNetworkCode);
    }
#endif

    // --- Screen ---

    /** Get the main screen bounds. */
    struct ScreenInfo {
        int width;
        int height;
        float scale;
        float brightness;
        int refresh_rate;
    };

    ScreenInfo main_screen_info() const {
        UIScreen *screen = [UIScreen mainScreen];
        ScreenInfo info{};
        CGRect bounds = screen.bounds;
        info.width   = static_cast<int>(bounds.size.width * screen.scale);
        info.height  = static_cast<int>(bounds.size.height * screen.scale);
        info.scale   = screen.scale;
        info.brightness = screen.brightness;

        if (@available(iOS 10.3, *)) {
            info.refresh_rate = screen.maximumFramesPerSecond;
        } else {
            info.refresh_rate = 60;
        }
        return info;
    }

    /** Get list of all connected screens (for external displays). */
    std::vector<ScreenInfo> all_screens_info() const {
        std::vector<ScreenInfo> result;
        for (UIScreen *screen in [UIScreen screens]) {
            ScreenInfo info{};
            CGRect bounds = screen.bounds;
            info.width   = static_cast<int>(bounds.size.width * screen.scale);
            info.height  = static_cast<int>(bounds.size.height * screen.scale);
            info.scale   = screen.scale;
            info.brightness = screen.brightness;
            if (@available(iOS 10.3, *)) {
                info.refresh_rate = screen.maximumFramesPerSecond;
            } else {
                info.refresh_rate = 60;
            }
            result.push_back(info);
        }
        return result;
    }

    // --- Process Info ---

    std::string process_name() const {
        return ns_to_std([[NSProcessInfo processInfo] processName]);
    }

    int process_id() const {
        return [[NSProcessInfo processInfo] processIdentifier];
    }

    std::string os_version_string() const {
        auto *osVer = [[NSProcessInfo processInfo] operatingSystemVersionString];
        return ns_to_std(osVer);
    }

    /**
     * Get a comprehensive device info struct as a formatted string.
     */
    std::string dump_all() const {
        std::ostringstream oss;
        oss << "Device:       " << device_name() << "\n";
        oss << "Model:        " << model() << " (" << hardware_model() << ")\n";
        oss << "Type:         " << device_type_string() << "\n";
        oss << "OS:           " << system_name() << " " << system_version() << "\n";
        oss << "Memory:       " << physical_memory_mb() << " MB\n";
        oss << "CPU Cores:    " << cpu_core_count() << "\n";
        oss << "Battery:      " << battery_percent() << "% (" << battery_state_string() << ")\n";
        oss << "Screen:       " << main_screen_info().width << "x"
             << main_screen_info().height << " @ " << main_screen_info().scale << "x\n";
        oss << "Disk Free:    " << (free_disk_space() / (1024 * 1024)) << " MB\n";
        oss << "Process:      " << process_name() << " (PID " << process_id() << ")\n";
        oss << "Memory Usage: " << process_memory_mb() << " MB\n";
        return oss.str();
    }
};

// ===========================================================================
//  SECTION 11 — AUDIO SESSION MANAGEMENT (AVAudioSession)
// ===========================================================================
//
//  AVAudioSession is the central audio management API on iOS.
//  It controls audio routing, categories, interruptions, and more.

struct AudioSessionManager {

    /** Initialise the audio session for playback and recording. */
    bool initialise_play_and_record(std::string &error_out) {
        AVAudioSession *session = [AVAudioSession sharedInstance];
        NSError *error = nil;

        // Set category to PlayAndRecord with default-to-speaker
        BOOL success = [session setCategory:AVAudioSessionCategoryPlayAndRecord
                                       mode:AVAudioSessionModeDefault
                                    options:AVAudioSessionCategoryOptionDefaultToSpeaker |
                                            AVAudioSessionCategoryOptionAllowBluetooth |
                                            AVAudioSessionCategoryOptionAllowBluetoothA2DP
                                      error:&error];
        if (!success) {
            error_out = ns_to_std(error.localizedDescription);
            IOS_LOGE("[ios] setCategory failed: {}", error_out);
            return false;
        }

        // Activate the session
        success = [session setActive:YES error:&error];
        if (!success) {
            error_out = ns_to_std(error.localizedDescription);
            IOS_LOGE("[ios] setActive failed: {}", error_out);
            return false;
        }

        IOS_LOGI("[ios] Audio session initialised (PlayAndRecord)");
        return true;
    }

    /** Initialise for playback only. */
    bool initialise_playback_only(std::string &error_out) {
        AVAudioSession *session = [AVAudioSession sharedInstance];
        NSError *error = nil;

        BOOL success = [session setCategory:AVAudioSessionCategoryPlayback
                                       mode:AVAudioSessionModeDefault
                                    options:0
                                      error:&error];
        if (!success) {
            error_out = ns_to_std(error.localizedDescription);
            IOS_LOGE("[ios] setCategory(Playback) failed: {}", error_out);
            return false;
        }

        success = [session setActive:YES error:&error];
        if (!success) {
            error_out = ns_to_std(error.localizedDescription);
            IOS_LOGE("[ios] setActive failed: {}", error_out);
            return false;
        }

        IOS_LOGI("[ios] Audio session initialised (Playback)");
        return true;
    }

    /** Initialise for recording only. */
    bool initialise_record_only(std::string &error_out) {
        AVAudioSession *session = [AVAudioSession sharedInstance];
        NSError *error = nil;

        BOOL success = [session setCategory:AVAudioSessionCategoryRecord
                                       mode:AVAudioSessionModeDefault
                                    options:0
                                      error:&error];
        if (!success) {
            error_out = ns_to_std(error.localizedDescription);
            IOS_LOGE("[ios] setCategory(Record) failed: {}", error_out);
            return false;
        }

        success = [session setActive:YES error:&error];
        if (!success) {
            error_out = ns_to_std(error.localizedDescription);
            return false;
        }

        IOS_LOGI("[ios] Audio session initialised (Record)");
        return true;
    }

    /** Deactivate the audio session. */
    void deactivate() {
        AVAudioSession *session = [AVAudioSession sharedInstance];
        NSError *error = nil;
        [session setActive:NO error:&error];
        if (error) {
            IOS_LOGE("[ios] Audio session deactivation error: {}",
                      ns_to_std(error.localizedDescription));
        } else {
            IOS_LOGI("[ios] Audio session deactivated");
        }
    }

    /** Set preferred sample rate. */
    bool set_preferred_sample_rate(double sample_rate, std::string &error_out) {
        AVAudioSession *session = [AVAudioSession sharedInstance];
        NSError *error = nil;
        BOOL ok = [session setPreferredSampleRate:sample_rate error:&error];
        if (!ok) {
            error_out = ns_to_std(error.localizedDescription);
            IOS_LOGE("[ios] setPreferredSampleRate({}) failed: {}", sample_rate, error_out);
        }
        return ok;
    }

    /** Get current sample rate. */
    double sample_rate() const {
        return [AVAudioSession sharedInstance].sampleRate;
    }

    /** Set preferred I/O buffer duration. */
    bool set_preferred_io_buffer_duration(double duration, std::string &error_out) {
        AVAudioSession *session = [AVAudioSession sharedInstance];
        NSError *error = nil;
        BOOL ok = [session setPreferredIOBufferDuration:duration error:&error];
        if (!ok) {
            error_out = ns_to_std(error.localizedDescription);
        }
        return ok;
    }

    /** Get current I/O buffer duration. */
    double io_buffer_duration() const {
        return [AVAudioSession sharedInstance].IOBufferDuration;
    }

    /** Get current output latency. */
    double output_latency() const {
        return [AVAudioSession sharedInstance].outputLatency;
    }

    /** Get current input latency. */
    double input_latency() const {
        return [AVAudioSession sharedInstance].inputLatency;
    }

    /** Get the current audio route description. */
    struct AudioRouteInfo {
        std::string category;
        std::string mode;
        std::vector<std::string> inputs;
        std::vector<std::string> outputs;
    };

    AudioRouteInfo current_route() const {
        AVAudioSession *session = [AVAudioSession sharedInstance];
        AudioRouteInfo info;

        info.category = ns_to_std(session.category);
        info.mode     = ns_to_std(session.mode);

        AVAudioSessionRouteDescription *route = session.currentRoute;
        for (AVAudioSessionPortDescription *port in route.inputs) {
            info.inputs.push_back(ns_to_std(port.portName));
        }
        for (AVAudioSessionPortDescription *port in route.outputs) {
            info.outputs.push_back(ns_to_std(port.portName));
        }

        return info;
    }

    /** Check if headphones are connected. */
    bool is_headphones_connected() const {
        AVAudioSessionRouteDescription *route =
            [AVAudioSession sharedInstance].currentRoute;
        for (AVAudioSessionPortDescription *port in route.outputs) {
            if ([port.portType isEqualToString:AVAudioSessionPortHeadphones] ||
                [port.portType isEqualToString:AVAudioSessionPortBluetoothA2DP] ||
                [port.portType isEqualToString:AVAudioSessionPortBluetoothHFP] ||
                [port.portType isEqualToString:AVAudioSessionPortBluetoothLE]) {
                return true;
            }
        }
        return false;
    }

    /** Check if Bluetooth audio is connected. */
    bool is_bluetooth_connected() const {
        AVAudioSessionRouteDescription *route =
            [AVAudioSession sharedInstance].currentRoute;
        for (AVAudioSessionPortDescription *port in route.outputs) {
            if ([port.portType isEqualToString:AVAudioSessionPortBluetoothA2DP] ||
                [port.portType isEqualToString:AVAudioSessionPortBluetoothHFP] ||
                [port.portType isEqualToString:AVAudioSessionPortBluetoothLE]) {
                return true;
            }
        }
        return false;
    }

    /** Check if another app is playing audio. */
    bool is_other_audio_playing() const {
        return [AVAudioSession sharedInstance].isOtherAudioPlaying;
    }

    /** Get the current audio output volume (system volume, 0.0-1.0). */
    float output_volume() const {
        return [AVAudioSession sharedInstance].outputVolume;
    }

    /** Register for audio route change notifications. */
    id add_route_change_observer(std::function<void(std::string reason)> callback) {
        id observer = [[NSNotificationCenter defaultCenter]
            addObserverForName:AVAudioSessionRouteChangeNotification
            object:nil
            queue:[NSOperationQueue mainQueue]
            usingBlock:^(NSNotification *note) {

                NSNumber *reasonNum = note.userInfo[AVAudioSessionRouteChangeReasonKey];
                std::string reason_str;

                switch ([reasonNum unsignedIntegerValue]) {
                case AVAudioSessionRouteChangeReasonUnknown:          reason_str = "unknown"; break;
                case AVAudioSessionRouteChangeReasonNewDeviceAvailable: reason_str = "new_device"; break;
                case AVAudioSessionRouteChangeReasonOldDeviceUnavailable: reason_str = "old_device_gone"; break;
                case AVAudioSessionRouteChangeReasonCategoryChange:   reason_str = "category_change"; break;
                case AVAudioSessionRouteChangeReasonOverride:         reason_str = "override"; break;
                case AVAudioSessionRouteChangeReasonWakeFromSleep:    reason_str = "wake_from_sleep"; break;
                case AVAudioSessionRouteChangeReasonNoSuitableRouteForCategory: reason_str = "no_route"; break;
                case AVAudioSessionRouteChangeReasonRouteConfigurationChange: reason_str = "config_change"; break;
                default: reason_str = "unknown"; break;
                }

                IOS_LOGI("[ios] Audio route changed: {}", reason_str);
                if (callback) callback(reason_str);
            }];

        return observer;
    }

    /** Remove a route change observer. */
    void remove_route_change_observer(id observer) {
        [[NSNotificationCenter defaultCenter] removeObserver:observer];
    }

    /** Register for audio interruption notifications. */
    id add_interruption_observer(std::function<void(bool began)> callback) {
        id observer = [[NSNotificationCenter defaultCenter]
            addObserverForName:AVAudioSessionInterruptionNotification
            object:nil
            queue:[NSOperationQueue mainQueue]
            usingBlock:^(NSNotification *note) {

                NSNumber *typeNum = note.userInfo[AVAudioSessionInterruptionTypeKey];
                bool began = ([typeNum unsignedIntegerValue] == AVAudioSessionInterruptionTypeBegan);

                IOS_LOGI("[ios] Audio interruption: {}", began ? "began" : "ended");
                if (callback) callback(began);
            }];

        return observer;
    }

    /** Remove an interruption observer. */
    void remove_interruption_observer(id observer) {
        [[NSNotificationCenter defaultCenter] removeObserver:observer];
    }

    /** Override output audio port to speaker. */
    bool override_to_speaker(std::string &error_out) {
        AVAudioSession *session = [AVAudioSession sharedInstance];
        NSError *error = nil;
        BOOL ok = [session overrideOutputAudioPort:AVAudioSessionPortOverrideSpeaker
                                              error:&error];
        if (!ok && error) {
            error_out = ns_to_std(error.localizedDescription);
        }
        return ok;
    }

    /** Request recording permission. */
    void request_record_permission(std::function<void(bool granted)> callback) {
        [[AVAudioSession sharedInstance]
            requestRecordPermission:^(BOOL granted) {
                IOS_LOGI("[ios] Record permission: {}", granted ? "granted" : "denied");
                if (callback) callback(granted);
            }];
    }

    /** Check if we can record (privacy + audio hardware). */
    bool is_recording_available() const {
        return [AVAudioSession sharedInstance].isInputAvailable;
    }
};

// ===========================================================================
//  SECTION 12 — TOUCH / GESTURE HANDLING
// ===========================================================================
//
//  Wrapping UIGestureRecognizer and UITouch handling for C++.
//  This enables the cppdesk engine to respond to taps, swipes, pans,
//  long presses, pinches, and rotations.

struct TouchHandler {
    // --- Recognizer wrapper types ---
    struct RecognizerHandle {
        UIGestureRecognizer *recognizer = nil;
        UIView *view = nil;
        int id = 0;
    };

    std::vector<RecognizerHandle> recognizers;
    std::atomic<int> next_id{1};

    /**
     * Add a tap gesture to the given UIView.
     *
     * @param view             The UIView to attach the recognizer to.
     * @param taps_required    Number of taps required (1=single, 2=double).
     * @param touches_required Number of fingers required.
     * @param callback         Called with the (x, y) location in the view.
     * @return A handle ID for later removal.
     */
    int add_tap_gesture(UIView *view,
                         int taps_required,
                         int touches_required,
                         std::function<void(float x, float y)> callback) {

        __block int handle_id = next_id.fetch_add(1);

        UITapGestureRecognizer *tap = [[UITapGestureRecognizer alloc]
            initWithTarget:nil action:nil];

        tap.numberOfTapsRequired    = taps_required;
        tap.numberOfTouchesRequired = touches_required;

        // Use a block-based action via UIGestureRecognizer category / subclass
        // We use associated objects to store the callback
        static const void *kCallbackKey = &kCallbackKey;
        static const void *kHandleIdKey = &kHandleIdKey;

        auto cb = new std::function<void(float, float)>(std::move(callback));

        objc_setAssociatedObject(tap, kCallbackKey,
            (__bridge id)((void *)cb), OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        objc_setAssociatedObject(tap, kHandleIdKey,
            @(handle_id), OBJC_ASSOCIATION_RETAIN_NONATOMIC);

        // Set up the action using a helper method on our custom recognizer
        [tap addTarget:[TouchRecognizerTarget shared]
                 action:@selector(handleTap:)];

        [view addGestureRecognizer:tap];
        view.userInteractionEnabled = YES;

        RecognizerHandle h;
        h.recognizer = tap;
        h.view = view;
        h.id = handle_id;
        recognizers.push_back(h);

        IOS_LOGD("[ios] Tap gesture added (id={}, taps={}, fingers={})",
                  handle_id, taps_required, touches_required);

        return handle_id;
    }

    /**
     * Add a long-press gesture.
     */
    int add_long_press_gesture(UIView *view,
                                double minimum_press_duration,
                                int touches_required,
                                std::function<void(float x, float y, bool began)> callback) {

        int handle_id = next_id.fetch_add(1);

        UILongPressGestureRecognizer *lp = [[UILongPressGestureRecognizer alloc]
            initWithTarget:nil action:nil];

        lp.minimumPressDuration = minimum_press_duration;
        lp.numberOfTouchesRequired = touches_required;

        static const void *kLPCallbackKey = &kLPCallbackKey;
        auto cb = new std::function<void(float, float, bool)>(std::move(callback));
        objc_setAssociatedObject(lp, kLPCallbackKey,
            (__bridge id)((void *)cb), OBJC_ASSOCIATION_RETAIN_NONATOMIC);

        [lp addTarget:[TouchRecognizerTarget shared]
               action:@selector(handleLongPress:)];

        [view addGestureRecognizer:lp];
        view.userInteractionEnabled = YES;

        RecognizerHandle h{lp, view, handle_id};
        recognizers.push_back(h);

        IOS_LOGD("[ios] Long-press gesture added (id={}, duration={}s)",
                  handle_id, minimum_press_duration);

        return handle_id;
    }

    /**
     * Add a pan (drag) gesture.
     */
    int add_pan_gesture(UIView *view,
                         int min_touches, int max_touches,
                         std::function<void(float dx, float dy,
                                             float velocity_x, float velocity_y,
                                             bool began, bool ended)> callback) {

        int handle_id = next_id.fetch_add(1);

        UIPanGestureRecognizer *pan = [[UIPanGestureRecognizer alloc]
            initWithTarget:nil action:nil];

        pan.minimumNumberOfTouches = min_touches;
        pan.maximumNumberOfTouches = max_touches;

        static const void *kPanCallbackKey = &kPanCallbackKey;
        auto cb = new std::function<void(float,float,float,float,bool,bool)>(
            std::move(callback));
        objc_setAssociatedObject(pan, kPanCallbackKey,
            (__bridge id)((void *)cb), OBJC_ASSOCIATION_RETAIN_NONATOMIC);

        [pan addTarget:[TouchRecognizerTarget shared]
                action:@selector(handlePan:)];

        [view addGestureRecognizer:pan];
        view.userInteractionEnabled = YES;

        RecognizerHandle h{pan, view, handle_id};
        recognizers.push_back(h);

        IOS_LOGD("[ios] Pan gesture added (id={}, min={}, max={})",
                  handle_id, min_touches, max_touches);

        return handle_id;
    }

    /**
     * Add a pinch gesture.
     */
    int add_pinch_gesture(UIView *view,
                           std::function<void(float scale, float velocity,
                                               bool began, bool ended)> callback) {

        int handle_id = next_id.fetch_add(1);

        UIPinchGestureRecognizer *pinch = [[UIPinchGestureRecognizer alloc]
            initWithTarget:nil action:nil];

        static const void *kPinchCallbackKey = &kPinchCallbackKey;
        auto cb = new std::function<void(float,float,bool,bool)>(std::move(callback));
        objc_setAssociatedObject(pinch, kPinchCallbackKey,
            (__bridge id)((void *)cb), OBJC_ASSOCIATION_RETAIN_NONATOMIC);

        [pinch addTarget:[TouchRecognizerTarget shared]
                  action:@selector(handlePinch:)];

        [view addGestureRecognizer:pinch];
        view.userInteractionEnabled = YES;

        RecognizerHandle h{pinch, view, handle_id};
        recognizers.push_back(h);

        IOS_LOGD("[ios] Pinch gesture added (id={})", handle_id);
        return handle_id;
    }

    /**
     * Add a rotation gesture.
     */
    int add_rotation_gesture(UIView *view,
                              std::function<void(float rotation_radians,
                                                  float velocity,
                                                  bool began, bool ended)> callback) {

        int handle_id = next_id.fetch_add(1);

        UIRotationGestureRecognizer *rot = [[UIRotationGestureRecognizer alloc]
            initWithTarget:nil action:nil];

        static const void *kRotCallbackKey = &kRotCallbackKey;
        auto cb = new std::function<void(float,float,bool,bool)>(std::move(callback));
        objc_setAssociatedObject(rot, kRotCallbackKey,
            (__bridge id)((void *)cb), OBJC_ASSOCIATION_RETAIN_NONATOMIC);

        [rot addTarget:[TouchRecognizerTarget shared]
                action:@selector(handleRotation:)];

        [view addGestureRecognizer:rot];
        view.userInteractionEnabled = YES;

        RecognizerHandle h{rot, view, handle_id};
        recognizers.push_back(h);

        IOS_LOGD("[ios] Rotation gesture added (id={})", handle_id);
        return handle_id;
    }

    /**
     * Add a swipe gesture.
     */
    int add_swipe_gesture(UIView *view,
                           int direction, // 1=right, 2=left, 4=up, 8=down
                           int touches_required,
                           std::function<void(int dir)> callback) {

        int handle_id = next_id.fetch_add(1);

        UISwipeGestureRecognizer *swipe = [[UISwipeGestureRecognizer alloc]
            initWithTarget:nil action:nil];
        swipe.direction = (UISwipeGestureRecognizerDirection)direction;
        swipe.numberOfTouchesRequired = touches_required;

        static const void *kSwipeCallbackKey = &kSwipeCallbackKey;
        auto cb = new std::function<void(int)>(std::move(callback));
        objc_setAssociatedObject(swipe, kSwipeCallbackKey,
            (__bridge id)((void *)cb), OBJC_ASSOCIATION_RETAIN_NONATOMIC);

        [swipe addTarget:[TouchRecognizerTarget shared]
                  action:@selector(handleSwipe:)];

        [view addGestureRecognizer:swipe];
        view.userInteractionEnabled = YES;

        RecognizerHandle h{swipe, view, handle_id};
        recognizers.push_back(h);

        std::string dir_str;
        if (direction & 1) dir_str += "right ";
        if (direction & 2) dir_str += "left ";
        if (direction & 4) dir_str += "up ";
        if (direction & 8) dir_str += "down ";
        IOS_LOGD("[ios] Swipe gesture added (id={}, dirs=[{}])", handle_id, dir_str);

        return handle_id;
    }

    /** Remove a gesture recognizer by handle ID. */
    void remove_gesture(int handle_id) {
        for (auto it = recognizers.begin(); it != recognizers.end(); ++it) {
            if (it->id == handle_id) {
                [it->view removeGestureRecognizer:it->recognizer];
                recognizers.erase(it);
                IOS_LOGD("[ios] Gesture removed (id={})", handle_id);
                return;
            }
        }
        IOS_LOGW("[ios] Gesture not found for removal (id={})", handle_id);
    }

    /** Remove all gestures from a specific view. */
    void remove_all_from_view(UIView *view) {
        for (auto it = recognizers.begin(); it != recognizers.end(); ) {
            if (it->view == view) {
                [it->view removeGestureRecognizer:it->recognizer];
                it = recognizers.erase(it);
            } else {
                ++it;
            }
        }
        IOS_LOGD("[ios] All gestures removed from view");
    }

    /** Remove all gestures managed by this handler. */
    void remove_all() {
        for (auto &h : recognizers) {
            [h.view removeGestureRecognizer:h.recognizer];
        }
        recognizers.clear();
        IOS_LOGI("[ios] All gestures removed");
    }
};

// ===========================================================================
//  Touch Recognizer Target (bridges ObjC -> C++ callbacks)
// ===========================================================================
//
//  Because UIGestureRecognizer target/action must be an ObjC method,
//  we use a singleton helper object that looks up the C++ callback via
//  associated objects on the recognizer.

@interface TouchRecognizerTarget : NSObject
+ (instancetype)shared;
- (void)handleTap:(UITapGestureRecognizer *)tap;
- (void)handleLongPress:(UILongPressGestureRecognizer *)lp;
- (void)handlePan:(UIPanGestureRecognizer *)pan;
- (void)handlePinch:(UIPinchGestureRecognizer *)pinch;
- (void)handleRotation:(UIRotationGestureRecognizer *)rot;
- (void)handleSwipe:(UISwipeGestureRecognizer *)swipe;
@end

@implementation TouchRecognizerTarget

+ (instancetype)shared {
    static TouchRecognizerTarget *instance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [[TouchRecognizerTarget alloc] init];
    });
    return instance;
}

static const void *kTapCbKey      = &kTapCbKey;
static const void *kLPCbKey       = &kLPCbKey;
static const void *kPanCbKey      = &kPanCbKey;
static const void *kPinchCbKey    = &kPinchCbKey;
static const void *kRotCbKey      = &kRotCbKey;
static const void *kSwipeCbKey    = &kSwipeCbKey;

- (void)handleTap:(UITapGestureRecognizer *)tap {
    if (tap.state != UIGestureRecognizerStateEnded) return;

    id cbObj = objc_getAssociatedObject(tap, kTapCbKey);
    if (!cbObj) return;
    auto *cb = (std::function<void(float, float)> *)(__bridge void *)cbObj;
    if (!cb || !*cb) return;

    CGPoint loc = [tap locationInView:tap.view];
    (*cb)((float)loc.x, (float)loc.y);
}

- (void)handleLongPress:(UILongPressGestureRecognizer *)lp {
    if (lp.state != UIGestureRecognizerStateBegan &&
        lp.state != UIGestureRecognizerStateEnded) return;

    id cbObj = objc_getAssociatedObject(lp, kLPCbKey);
    if (!cbObj) return;
    auto *cb = (std::function<void(float, float, bool)> *)(__bridge void *)cbObj;
    if (!cb || !*cb) return;

    CGPoint loc = [lp locationInView:lp.view];
    bool began = (lp.state == UIGestureRecognizerStateBegan);
    (*cb)((float)loc.x, (float)loc.y, began);
}

- (void)handlePan:(UIPanGestureRecognizer *)pan {
    id cbObj = objc_getAssociatedObject(pan, kPanCbKey);
    if (!cbObj) return;
    auto *cb = (std::function<void(float,float,float,float,bool,bool)> *)(__bridge void *)cbObj;
    if (!cb || !*cb) return;

    CGPoint translation = [pan translationInView:pan.view];
    CGPoint velocity    = [pan velocityInView:pan.view];

    bool began = (pan.state == UIGestureRecognizerStateBegan);
    bool ended = (pan.state == UIGestureRecognizerStateEnded ||
                   pan.state == UIGestureRecognizerStateCancelled);

    (*cb)((float)translation.x, (float)translation.y,
          (float)velocity.x, (float)velocity.y, began, ended);

    if (ended) {
        [pan setTranslation:CGPointZero inView:pan.view];
    }
}

- (void)handlePinch:(UIPinchGestureRecognizer *)pinch {
    id cbObj = objc_getAssociatedObject(pinch, kPinchCbKey);
    if (!cbObj) return;
    auto *cb = (std::function<void(float,float,bool,bool)> *)(__bridge void *)cbObj;
    if (!cb || !*cb) return;

    bool began = (pinch.state == UIGestureRecognizerStateBegan);
    bool ended = (pinch.state == UIGestureRecognizerStateEnded ||
                   pinch.state == UIGestureRecognizerStateCancelled);

    (*cb)((float)pinch.scale, (float)pinch.velocity, began, ended);

    if (ended) pinch.scale = 1.0f;
}

- (void)handleRotation:(UIRotationGestureRecognizer *)rot {
    id cbObj = objc_getAssociatedObject(rot, kRotCbKey);
    if (!cbObj) return;
    auto *cb = (std::function<void(float,float,bool,bool)> *)(__bridge void *)cbObj;
    if (!cb || !*cb) return;

    bool began = (rot.state == UIGestureRecognizerStateBegan);
    bool ended = (rot.state == UIGestureRecognizerStateEnded ||
                   rot.state == UIGestureRecognizerStateCancelled);

    (*cb)((float)rot.rotation, (float)rot.velocity, began, ended);

    if (ended) rot.rotation = 0.0f;
}

- (void)handleSwipe:(UISwipeGestureRecognizer *)swipe {
    if (swipe.state != UIGestureRecognizerStateEnded) return;

    id cbObj = objc_getAssociatedObject(swipe, kSwipeCbKey);
    if (!cbObj) return;
    auto *cb = (std::function<void(int)> *)(__bridge void *)cbObj;
    if (!cb || !*cb) return;

    (*cb)((int)swipe.direction);
}

@end  // TouchRecognizerTarget

// ===========================================================================
//  SECTION 13 — REACHABILITY / NETWORK MONITORING
// ===========================================================================
//
//  Uses SCNetworkReachability (SystemConfiguration) to monitor
//  network connectivity changes.

struct ReachabilityMonitor {
    SCNetworkReachabilityRef reachability = nullptr;
    std::function<void(std::string status)> on_change;
    bool is_monitoring = false;

    /** Start monitoring network reachability. */
    bool start_monitoring(const std::string &hostname,
                           std::function<void(std::string status)> callback) {

        on_change = std::move(callback);

        NSString *host = std_to_ns(hostname.empty() ? "www.apple.com" : hostname);

        // Create reachability reference for host
        reachability = SCNetworkReachabilityCreateWithName(
            kCFAllocatorDefault, [host UTF8String]);

        if (!reachability) {
            IOS_LOGE("[ios] SCNetworkReachabilityCreateWithName failed");
            return false;
        }

        // Set up the callback
        SCNetworkReachabilityContext context = {
            0,
            (__bridge void *)self,
            nullptr,  // retain
            nullptr,  // release
            nullptr   // copyDescription
        };

        if (!SCNetworkReachabilitySetCallback(reachability,
                                               _reachability_callback,
                                               &context)) {
            IOS_LOGE("[ios] SCNetworkReachabilitySetCallback failed");
            CFRelease(reachability);
            reachability = nullptr;
            return false;
        }

        if (!SCNetworkReachabilityScheduleWithRunLoop(
                reachability,
                CFRunLoopGetMain(),
                kCFRunLoopDefaultMode)) {
            IOS_LOGE("[ios] SCNetworkReachabilityScheduleWithRunLoop failed");
            CFRelease(reachability);
            reachability = nullptr;
            return false;
        }

        is_monitoring = true;
        IOS_LOGI("[ios] Reachability monitoring started for: {}",
                  hostname.empty() ? "default" : hostname);

        // Get initial status
        check_status();
        return true;
    }

    /** Stop monitoring. */
    void stop_monitoring() {
        if (!reachability || !is_monitoring) return;

        SCNetworkReachabilityUnscheduleFromRunLoop(
            reachability, CFRunLoopGetMain(), kCFRunLoopDefaultMode);
        CFRelease(reachability);
        reachability = nullptr;
        is_monitoring = false;

        IOS_LOGI("[ios] Reachability monitoring stopped");
    }

    /** Check current status synchronously. */
    std::string current_status() const {
        if (!reachability) return "not_monitoring";

        SCNetworkReachabilityFlags flags;
        if (!SCNetworkReachabilityGetFlags(reachability, &flags)) {
            return "error";
        }

        return _flags_to_string(flags);
    }

private:
    void check_status() {
        if (!reachability || !on_change) return;

        SCNetworkReachabilityFlags flags;
        if (SCNetworkReachabilityGetFlags(reachability, &flags)) {
            on_change(_flags_to_string(flags));
        }
    }

    static std::string _flags_to_string(SCNetworkReachabilityFlags flags) {
        if (!(flags & kSCNetworkReachabilityFlagsReachable)) {
            return "not_reachable";
        }
        if (!(flags & kSCNetworkReachabilityFlagsConnectionRequired)) {
            return "wifi";  // reachable without connection
        }
        if (flags & kSCNetworkReachabilityFlagsConnectionOnDemand ||
            flags & kSCNetworkReachabilityFlagsConnectionOnTraffic) {
            if (!(flags & kSCNetworkReachabilityFlagsInterventionRequired)) {
                return "on_demand";  // reachable on demand
            }
        }
        if (flags & kSCNetworkReachabilityFlagsIsWWAN) {
            return "cellular";
        }
        return "reachable";
    }

    static void _reachability_callback(SCNetworkReachabilityRef target,
                                        SCNetworkReachabilityFlags flags,
                                        void *info) {
        auto *self = (__bridge ReachabilityMonitor *)info;
        if (self && self->on_change) {
            self->on_change(_flags_to_string(flags));
        }
    }
};

// ===========================================================================
//  SECTION 14 — KEYCHAIN SECURE STORAGE
// ===========================================================================
//
//  The iOS Keychain provides secure, encrypted storage for sensitive
//  data such as passwords, tokens, and cryptographic keys.

struct KeychainStore {
    std::string service_name;

    explicit KeychainStore(const std::string &service)
        : service_name(service) {}

    /** Store a password/token in the Keychain. */
    bool set(const std::string &key, const std::string &value) {
        // First, try to delete any existing item with this key
        NSDictionary *deleteQuery = @{
            (__bridge id)kSecClass       : (__bridge id)kSecClassGenericPassword,
            (__bridge id)kSecAttrService : std_to_ns(service_name),
            (__bridge id)kSecAttrAccount : std_to_ns(key)
        };
        SecItemDelete((__bridge CFDictionaryRef)deleteQuery);

        // Now add the new item
        NSDictionary *addQuery = @{
            (__bridge id)kSecClass       : (__bridge id)kSecClassGenericPassword,
            (__bridge id)kSecAttrService : std_to_ns(service_name),
            (__bridge id)kSecAttrAccount : std_to_ns(key),
            (__bridge id)kSecValueData   : [std_to_ns(value) dataUsingEncoding:NSUTF8StringEncoding],
            (__bridge id)kSecAttrAccessible : (__bridge id)kSecAttrAccessibleWhenUnlockedThisDeviceOnly
        };

        OSStatus status = SecItemAdd((__bridge CFDictionaryRef)addQuery, nullptr);
        if (status == errSecSuccess) {
            IOS_LOGD("[ios] Keychain set: {}", key);
            return true;
        }

        IOS_LOGE("[ios] Keychain set failed for '{}': status={}", key, (int)status);
        return false;
    }

    /** Retrieve a value from the Keychain. */
    std::optional<std::string> get(const std::string &key) const {
        NSDictionary *query = @{
            (__bridge id)kSecClass       : (__bridge id)kSecClassGenericPassword,
            (__bridge id)kSecAttrService : std_to_ns(service_name),
            (__bridge id)kSecAttrAccount : std_to_ns(key),
            (__bridge id)kSecReturnData  : @YES,
            (__bridge id)kSecMatchLimit  : (__bridge id)kSecMatchLimitOne
        };

        CFTypeRef result = nullptr;
        OSStatus status = SecItemCopyMatching((__bridge CFDictionaryRef)query, &result);

        if (status == errSecSuccess && result) {
            NSData *data = (__bridge_transfer NSData *)result;
            NSString *str = [[NSString alloc] initWithData:data
                                                  encoding:NSUTF8StringEncoding];
            if (str) {
                return ns_to_std(str);
            }
        }

        if (status != errSecItemNotFound) {
            IOS_LOGE("[ios] Keychain get failed for '{}': status={}", key, (int)status);
        }
        return std::nullopt;
    }

    /** Delete a value from the Keychain. */
    bool remove(const std::string &key) {
        NSDictionary *query = @{
            (__bridge id)kSecClass       : (__bridge id)kSecClassGenericPassword,
            (__bridge id)kSecAttrService : std_to_ns(service_name),
            (__bridge id)kSecAttrAccount : std_to_ns(key)
        };

        OSStatus status = SecItemDelete((__bridge CFDictionaryRef)query);
        if (status == errSecSuccess || status == errSecItemNotFound) {
            IOS_LOGD("[ios] Keychain removed: {}", key);
            return true;
        }

        IOS_LOGE("[ios] Keychain remove failed for '{}': status={}", key, (int)status);
        return false;
    }

    /** Delete all items for this service. */
    bool clear() {
        NSDictionary *query = @{
            (__bridge id)kSecClass       : (__bridge id)kSecClassGenericPassword,
            (__bridge id)kSecAttrService : std_to_ns(service_name)
        };

        OSStatus status = SecItemDelete((__bridge CFDictionaryRef)query);
        IOS_LOGI("[ios] Keychain cleared for service '{}' (status={})",
                  service_name, (int)status);
        return status == errSecSuccess || status == errSecItemNotFound;
    }

    /** Check if a key exists. */
    bool contains(const std::string &key) const {
        NSDictionary *query = @{
            (__bridge id)kSecClass       : (__bridge id)kSecClassGenericPassword,
            (__bridge id)kSecAttrService : std_to_ns(service_name),
            (__bridge id)kSecAttrAccount : std_to_ns(key),
            (__bridge id)kSecMatchLimit  : (__bridge id)kSecMatchLimitOne
        };

        OSStatus status = SecItemCopyMatching((__bridge CFDictionaryRef)query, nullptr);
        return status == errSecSuccess;
    }
};

// ===========================================================================
//  SECTION 15 — SYSTEM-WIDE KEYBOARD INPUT
// ===========================================================================
//
//  iOS does not allow global keyboard hooks like macOS.
//  However, we can handle keyboard input within our app using
//  UIKeyCommand (iOS 7+) and pressesBegan (iOS 13.4+ for hardware
//  keyboards).

struct KeyboardController {

    /** Check if a hardware keyboard is connected. */
    bool is_hardware_keyboard_connected() const {
        // iOS doesn't expose this directly, but we can infer from
        // UIKeyboardResponder behavior.  We use a best-effort heuristic.
        if (@available(iOS 14.0, *)) {
            // GCKeyboard is available but private-ish.
            // Best effort: check if the software keyboard frame is zero.
        }
        return false; // Default: assume not connected
    }

    /**
     * Add key commands to a UIResponder.
     *
     * This should be overridden in a UIViewController or UIView subclass's
     * `keyCommands` property.  Here we provide the data to generate them.
     */
    struct KeyCommand {
        std::string input;           // e.g., "c", UIKeyInputUpArrow
        int modifier_flags = 0;     // UIKeyModifierFlags: 1=alphaShift, 2=shift, 4=control, 8=alternate, 16=command, 32=numericPad
        std::string discoverability_title;
        std::function<void()> action;
    };

    std::vector<UIKeyCommand *> build_key_commands(
        const std::vector<KeyCommand> &commands) const {

        NSMutableArray<UIKeyCommand *> *result = [NSMutableArray array];

        for (const auto &cmd : commands) {
            UIKeyCommand *kc = [UIKeyCommand
                keyCommandWithInput:std_to_ns(cmd.input)
                modifierFlags:(UIKeyModifierFlags)cmd.modifier_flags
                action:@selector(cppdesk_key_command:)];

            if (!cmd.discoverability_title.empty()) {
                kc.discoverabilityTitle = std_to_ns(cmd.discoverability_title);
            }

            [result addObject:kc];
        }

        return std::vector<UIKeyCommand *>(
            (UIKeyCommand **)result.bytes,
            (UIKeyCommand **)result.bytes + result.count);
    }

    /**
     * Get the current keyboard layout / language.
     */
    std::string current_keyboard_language() const {
        UITextInputMode *mode = [UITextInputMode activeInputModes].firstObject;
        if (!mode) return "unknown";
        return ns_to_std(mode.primaryLanguage);
    }
};

// ===========================================================================
//  SECTION 16 — SCREEN MIRRORING / AIRPLAY DETECTION
// ===========================================================================
//
//  Detects when the app's content is being mirrored via AirPlay or
//  when an external display is connected.

struct AirPlayMonitor {
    id screen_connect_observer = nil;
    id screen_disconnect_observer = nil;
    id capture_session_observer = nil;

    struct AirPlayState {
        int  external_screen_count = 0;
        bool is_airplay_mirroring  = false;
        bool is_screen_being_captured = false; // iOS 11+
    };

    std::function<void(AirPlayState)> on_state_change;
    AirPlayState current;

    /** Start monitoring screen connections and AirPlay status. */
    void start_monitoring(std::function<void(AirPlayState)> callback) {
        on_state_change = std::move(callback);

        // --- Screen connection notifications ---
        screen_connect_observer = [[NSNotificationCenter defaultCenter]
            addObserverForName:UIScreenDidConnectNotification
            object:nil
            queue:[NSOperationQueue mainQueue]
            usingBlock:^(NSNotification *note) {
                self->current.external_screen_count =
                    (int)[UIScreen screens].count - 1; // minus main screen
                self->current.is_airplay_mirroring =
                    (self->current.external_screen_count > 0);

                UIScreen *screen = note.object;
                IOS_LOGI("[ios] External screen connected: {}x{}",
                          (int)screen.bounds.size.width,
                          (int)screen.bounds.size.height);

                if (self->on_state_change) {
                    self->on_state_change(self->current);
                }
            }];

        screen_disconnect_observer = [[NSNotificationCenter defaultCenter]
            addObserverForName:UIScreenDidDisconnectNotification
            object:nil
            queue:[NSOperationQueue mainQueue]
            usingBlock:^(NSNotification *note) {
                self->current.external_screen_count =
                    (int)[UIScreen screens].count - 1;
                self->current.is_airplay_mirroring =
                    (self->current.external_screen_count > 0);

                IOS_LOGI("[ios] External screen disconnected");

                if (self->on_state_change) {
                    self->on_state_change(self->current);
                }
            }];

        // --- Screen capture detection (iOS 11+) ---
        if (@available(iOS 11.0, *)) {
            capture_session_observer = [[NSNotificationCenter defaultCenter]
                addObserverForName:UIScreenCapturedDidChangeNotification
                object:nil
                queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification *note) {
                    self->current.is_screen_being_captured =
                        [UIScreen mainScreen].isCaptured;

                    IOS_LOGI("[ios] Screen capture changed: {}",
                              self->current.is_screen_being_captured ?
                              "capturing" : "not capturing");

                    if (self->on_state_change) {
                        self->on_state_change(self->current);
                    }
                }];

            // Get initial state
            current.is_screen_being_captured = [UIScreen mainScreen].isCaptured;
        }

        // Get initial screen count
        current.external_screen_count = (int)[UIScreen screens].count - 1;
        current.is_airplay_mirroring  = (current.external_screen_count > 0);

        IOS_LOGI("[ios] AirPlay/screen monitoring started (external={}, capturing={})",
                  current.external_screen_count, current.is_screen_being_captured);

        if (on_state_change) {
            on_state_change(current);
        }
    }

    /** Stop monitoring. */
    void stop_monitoring() {
        if (screen_connect_observer) {
            [[NSNotificationCenter defaultCenter]
                removeObserver:screen_connect_observer];
            screen_connect_observer = nil;
        }
        if (screen_disconnect_observer) {
            [[NSNotificationCenter defaultCenter]
                removeObserver:screen_disconnect_observer];
            screen_disconnect_observer = nil;
        }
        if (capture_session_observer) {
            [[NSNotificationCenter defaultCenter]
                removeObserver:capture_session_observer];
            capture_session_observer = nil;
        }
        IOS_LOGI("[ios] AirPlay/screen monitoring stopped");
    }

    /** Get a list of connected external screens with their properties. */
    struct ExternalScreenInfo {
        std::string name;
        int width;
        int height;
        float scale;
        bool is_airplay;
    };

    std::vector<ExternalScreenInfo> get_external_screens() const {
        std::vector<ExternalScreenInfo> result;
        NSArray<UIScreen *> *screens = [UIScreen screens];

        // screens[0] is always the main screen; the rest are external
        for (NSUInteger i = 1; i < screens.count; ++i) {
            UIScreen *screen = screens[i];
            ExternalScreenInfo info;
            info.name    = ns_to_std(screen.mirroredScreen ? @"Mirrored" : @"External");
            info.width   = (int)(screen.bounds.size.width * screen.scale);
            info.height  = (int)(screen.bounds.size.height * screen.scale);
            info.scale   = screen.scale;
            info.is_airplay = (screen.mirroredScreen != nil);
            result.push_back(info);
        }

        return result;
    }
};

// ===========================================================================
//  SECTION 17 — PLATFORM MANAGER / C API EXPORT
// ===========================================================================
//
//  Central manager that owns all platform subsystems and provides a
//  clean C++ interface for the rest of the cppdesk engine.

struct IOSPlatformManager {
    // --- Subsystem instances ---
    ScreenBroadcast         screen_broadcast;
    AppLifecycleDelegate    app_lifecycle;
    PacketTunnelProvider    packet_tunnel;
    NotificationScheduler   notifications;
    BackgroundTaskManager   background_tasks;
    DocumentPicker          document_picker;
    ClipboardManager        clipboard;
    DeviceInfo              device_info;
    AudioSessionManager     audio_session;
    TouchHandler            touch_handler;
    ReachabilityMonitor     reachability;
    KeychainStore           keychain{"com.cppdesk.keychain"};
    KeyboardController      keyboard;
    AirPlayMonitor          airplay;

    // --- Initialisation ---
    bool initialised = false;

    void initialise() {
        if (initialised) return;

        IOS_LOGI("[ios] === IOSPlatformManager initialising ===");
        IOS_LOGI("[ios] Device: {}", device_info.device_name());
        IOS_LOGI("[ios] OS: {} {}", device_info.system_name(), device_info.system_version());
        IOS_LOGI("[ios] Model: {} ({})", device_info.model(), device_info.hardware_model());
        IOS_LOGI("[ios] Memory: {} MB, CPU cores: {}",
                  device_info.physical_memory_mb(), device_info.cpu_core_count());

        initialised = true;
        IOS_LOGI("[ios] IOSPlatformManager initialised successfully");
    }

    void shutdown() {
        IOS_LOGI("[ios] === IOSPlatformManager shutting down ===");
        reachability.stop_monitoring();
        airplay.stop_monitoring();
        audio_session.deactivate();
        touch_handler.remove_all();
        initialised = false;
        IOS_LOGI("[ios] IOSPlatformManager shutdown complete");
    }

    // --- Convenience: dump full system state ---
    std::string dump_state() const {
        std::ostringstream oss;
        oss << "=== cppdesk iOS Platform State ===\n";
        oss << device_info.dump_all();
        oss << "Audio Route:\n";
        auto route = audio_session.current_route();
        oss << "  Category: " << route.category << "\n";
        oss << "  Mode:     " << route.mode << "\n";
        for (auto &in : route.inputs)  oss << "  Input:    " << in << "\n";
        for (auto &out : route.outputs) oss << "  Output:   " << out << "\n";
        oss << "  Latency:  in=" << audio_session.input_latency() * 1000
             << "ms out=" << audio_session.output_latency() * 1000 << "ms\n";
        oss << "Broadcast:  " << (screen_broadcast.broadcasting.load() ? "active" : "idle") << "\n";
        oss << "App State:  " << app_lifecycle.app_state_string() << "\n";
        oss << "Network:    " << reachability.current_status() << "\n";
        oss << "Tunnel:     " << packet_tunnel.connection_status() << "\n";
        oss << "Clipboard:  items=" << clipboard.item_count()
             << " change_count=" << clipboard.change_count() << "\n";
        oss << "Gestures:   " << touch_handler.recognizers.size() << "\n";
        return oss.str();
    }
};

// ===========================================================================
//  Global platform manager instance
// ===========================================================================
static std::unique_ptr<IOSPlatformManager> g_platform_manager;

IOSPlatformManager &platform() {
    if (!g_platform_manager) {
        g_platform_manager = std::make_unique<IOSPlatformManager>();
        g_platform_manager->initialise();
    }
    return *g_platform_manager;
}

}  // namespace cppdesk::platform::ios

// ===========================================================================
//  extern "C" API for interoperability
// ===========================================================================
//
//  These C-linkage functions can be called from Swift, ObjC, or other
//  C-compatible languages, bridging into the C++ platform manager.

extern "C" {

/** Initialise the iOS platform layer. */
void cppdesk_ios_init(void) {
    cppdesk::platform::ios::platform().initialise();
}

/** Shut down the iOS platform layer. */
void cppdesk_ios_shutdown(void) {
    cppdesk::platform::ios::platform().shutdown();
}

/** Get a JSON-formatted dump of the current platform state. */
const char *cppdesk_ios_dump_state(void) {
    static std::string state;
    state = cppdesk::platform::ios::platform().dump_state();
    return state.c_str();
}

/** Get battery level as percentage (-1 if unknown). */
int cppdesk_ios_battery_percent(void) {
    return cppdesk::platform::ios::platform().device_info.battery_percent();
}

/** Get battery state string. */
const char *cppdesk_ios_battery_state(void) {
    static std::string s;
    s = cppdesk::platform::ios::platform().device_info.battery_state_string();
    return s.c_str();
}

/** Get the clipboard string (returns nullptr if empty/not a string). */
const char *cppdesk_ios_clipboard_get_string(void) {
    static std::string s;
    auto opt = cppdesk::platform::ios::platform().clipboard.get_string();
    if (opt) { s = *opt; return s.c_str(); }
    return nullptr;
}

/** Set the clipboard string. */
void cppdesk_ios_clipboard_set_string(const char *text) {
    cppdesk::platform::ios::platform().clipboard.set_string(text ? text : "");
}

/** Begin a background task.  Returns non-zero task ID on success. */
int cppdesk_ios_begin_background_task(const char *name) {
    auto &mgr = cppdesk::platform::ios::platform().background_tasks;
    if (mgr.begin_task(name ? name : "cppdesk")) {
        return (int)mgr.current_task;
    }
    return 0;
}

/** End a background task. */
void cppdesk_ios_end_background_task(int task_id) {
    (void)task_id;
    cppdesk::platform::ios::platform().background_tasks.end_task();
}

/** Get remaining background time in seconds. */
double cppdesk_ios_background_time_remaining(void) {
    return cppdesk::platform::ios::platform().background_tasks.time_remaining();
}

/** Schedule a local notification after N seconds. */
void cppdesk_ios_schedule_notification(const char *title,
                                        const char *body,
                                        double seconds_from_now) {
    cppdesk::platform::ios::platform().notifications.schedule_time_interval(
        "cppdesk_auto", title ? title : "", body ? body : "", seconds_from_now);
}

/** Check if headphones are connected. */
int cppdesk_ios_headphones_connected(void) {
    return cppdesk::platform::ios::platform().audio_session.is_headphones_connected() ? 1 : 0;
}

}  // extern "C"

// ===========================================================================
//  End of platform implementation
// ===========================================================================

#endif  // TARGET_OS_IOS
#endif  // __APPLE__
