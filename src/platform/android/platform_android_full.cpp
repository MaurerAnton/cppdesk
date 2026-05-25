/**
 * platform_android_full.cpp — Comprehensive Android platform implementation
 *
 * This file implements the full Android platform backend for cppdesk,
 * covering screen capture, audio, input injection, permissions, foreground
 * services, battery optimisation, SAF file access, and more — all via
 * JNI calls into the Android SDK / NDK.
 *
 * Features:
 *   1.  JNI helper infrastructure  (class/method caching, error checking)
 *   2.  Android MediaProjection API for screen capture          (§ Capture)
 *   3.  MediaCodec hardware encoder integration                (§ Encoder)
 *   4.  AudioRecord for microphone capture                     (§ Mic)
 *   5.  AudioTrack for audio playback                          (§ Playback)
 *   6.  AccessibilityService for input injection               (§ Input)
 *   7.  Foreground service for background operation            (§ Service)
 *   8.  WakeLock management (PowerManager.WakeLock)            (§ WakeLock)
 *   9.  Notification channel setup                             (§ Notify)
 *  10.  Display metrics & multi-display support                 (§ Display)
 *  11.  Battery optimisation exemption                          (§ Battery)
 *  12.  File access via SAF (Storage Access Framework)          (§ SAF)
 *  13.  Permission handling                                     (§ Perm)
 *  14.  Misc helpers (Clipboard, URIs, System bars, Sensors)    (§ Misc)
 *
 *  Target:  C++20, spdlog logging, Android API 26+.
 *  Guarded  by #ifdef __ANDROID__.
 */

// ---------------------------------------------------------------------------
//  Preamble
// ---------------------------------------------------------------------------
#ifdef __ANDROID__

#include <jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/hardware_buffer.h>
#include <android/hardware_buffer_jni.h>

#include <spdlog/spdlog.h>

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
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

// ---------------------------------------------------------------------------
//  Convenience macros for Android logging (fallback when spdlog unavailable)
// ---------------------------------------------------------------------------
#define ANDROID_LOG_TAG "cppdesk_native"

#ifndef NDEBUG
#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, ANDROID_LOG_TAG, __VA_ARGS__)
#else
#define ALOGV(...) ((void)0)
#endif
#define ALOGD(...) __android_log_print(ANDROID_LOG_DEBUG, ANDROID_LOG_TAG, __VA_ARGS__)
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO,  ANDROID_LOG_TAG, __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN,  ANDROID_LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, ANDROID_LOG_TAG, __VA_ARGS__)

// ===========================================================================
//  namespace cppdesk::platform::android
// ===========================================================================
namespace cppdesk::platform::android {

// ---------------------------------------------------------------------------
//  Forward declarations
// ---------------------------------------------------------------------------
struct JniContext;
struct MediaProjectionCapture;
struct MediaCodecEncoder;
struct AudioCapturer;
struct AudioPlayer;
struct InputInjector;
struct ForegroundService;
struct WakeLockManager;
struct NotificationHelper;
struct DisplayInfo;
struct BatteryHelper;
struct SafHelper;
struct PermissionHelper;

// ===========================================================================
//  SECTION 1 — JNI HELPER INFRASTRUCTURE
// ===========================================================================

/**
 * Thread-local JNIEnv pointer.
 *
 * On Android, JNIEnv is thread-specific.  We always obtain it from the
 * JavaVM that was handed to us at library load time (JNI_OnLoad).
 */
static JavaVM *g_jvm = nullptr;

inline JavaVM *jvm() noexcept { return g_jvm; }

/** Obtain JNIEnv for the calling thread, attaching if necessary. */
inline JNIEnv *get_env() {
    if (!g_jvm) return nullptr;
    JNIEnv *env = nullptr;
    jint rc = g_jvm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6);
    if (rc == JNI_EDETACHED) {
        rc = g_jvm->AttachCurrentThread(&env, nullptr);
        if (rc != JNI_OK) {
            ALOGE("Failed to attach thread to JVM: %d", rc);
            return nullptr;
        }
    } else if (rc != JNI_OK) {
        ALOGE("GetEnv failed: %d", rc);
        return nullptr;
    }
    return env;
}

/**
 * RAII helper that ensures DetachCurrentThread is called if we attached.
 */
struct [[nodiscard]] JniEnvGuard {
    JNIEnv *env = nullptr;
    bool attached = false;

    explicit JniEnvGuard() {
        if (!g_jvm) return;
        jint rc = g_jvm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6);
        if (rc == JNI_EDETACHED) {
            rc = g_jvm->AttachCurrentThread(&env, nullptr);
            attached = (rc == JNI_OK);
        }
    }
    ~JniEnvGuard() {
        if (attached && g_jvm) g_jvm->DetachCurrentThread();
    }
    JniEnvGuard(const JniEnvGuard &) = delete;
    JniEnvGuard &operator=(const JniEnvGuard &) = delete;
};

// ---------------------------------------------------------------------------
//  Global / cached Java class references
// ---------------------------------------------------------------------------

/**
 * Lightweight class-cache entry.
 *
 * We use GlobalRefs to keep classes alive across threads.
 */
struct JniClassCache {
    jclass MediaProjection        = nullptr;
    jclass MediaProjectionManager = nullptr;
    jclass MediaCodec             = nullptr;
    jclass MediaFormat            = nullptr;
    jclass ByteBuffer             = nullptr;
    jclass BufferInfo             = nullptr;
    jclass AudioRecord            = nullptr;
    jclass AudioTrack             = nullptr;
    jclass AudioManager           = nullptr;
    jclass AudioFormatBuilder     = nullptr;
    jclass PowerManager           = nullptr;
    jclass WakeLock               = nullptr;
    jclass NotificationManager    = nullptr;
    jclass NotificationChannel    = nullptr;
    jclass NotificationBuilder    = nullptr;
    jclass Intent                 = nullptr;
    jclass PendingIntent          = nullptr;
    jclass Context                = nullptr;
    jclass Service                = nullptr;
    jclass DisplayManager         = nullptr;
    jclass Display                = nullptr;
    jclass DisplayMetrics         = nullptr;
    jclass WindowManager          = nullptr;
    jclass Point                  = nullptr;
    jclass Rect                   = nullptr;
    jclass Surface                = nullptr;
    jclass ImageReader            = nullptr;
    jclass AccessibilityService   = nullptr;
    jclass AccessibilityEvent     = nullptr;
    jclass GestureDescription     = nullptr;
    jclass GestureStroke          = nullptr;
    jclass Path                   = nullptr;
    jclass MotionEvent            = nullptr;
    jclass KeyEvent               = nullptr;
    jclass InputManager           = nullptr;
    jclass Uri                    = nullptr;
    jclass DocumentsContract      = nullptr;
    jclass ContentResolver        = nullptr;
    jclass DocumentFile           = nullptr;
    jclass File                   = nullptr;
    jclass Activity               = nullptr;
    jclass Bundle                 = nullptr;
    jclass Handler                = nullptr;
    jclass Looper                 = nullptr;
    jclass HandlerThread          = nullptr;
    jclass Executors              = nullptr;
    jclass CompletableFuture      = nullptr;
    jclass AtomicBoolean          = nullptr;
    jclass Size                   = nullptr;
    jclass Configuration          = nullptr;
    jclass Resources              = nullptr;
    jclass Build_VERSION          = nullptr;
    jclass Build_VERSION_CODES    = nullptr;

    bool loaded = false;
};

static JniClassCache g_cls;

/**
 * Convenience: find a class, make it a global ref, and store in cache.
 */
inline jclass cache_class(JNIEnv *env, const char *name) {
    jclass local = env->FindClass(name);
    if (!local) {
        ALOGW("FindClass(%s) returned null", name);
        if (env->ExceptionCheck()) env->ExceptionClear();
        return nullptr;
    }
    jclass global = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
    return global;
}

/**
 * Initialise the global class cache.  Call once from JNI_OnLoad or
 * from the first platform entry-point with a valid JNIEnv.
 */
static void init_class_cache(JNIEnv *env) {
    if (g_cls.loaded) return;

    g_cls.MediaProjection        = cache_class(env, "android/media/projection/MediaProjection");
    g_cls.MediaProjectionManager = cache_class(env, "android/media/projection/MediaProjectionManager");
    g_cls.MediaCodec             = cache_class(env, "android/media/MediaCodec");
    g_cls.MediaFormat            = cache_class(env, "android/media/MediaFormat");
    g_cls.ByteBuffer             = cache_class(env, "java/nio/ByteBuffer");
    g_cls.BufferInfo             = cache_class(env, "android/media/MediaCodec$BufferInfo");
    g_cls.AudioRecord            = cache_class(env, "android/media/AudioRecord");
    g_cls.AudioTrack             = cache_class(env, "android/media/AudioTrack");
    g_cls.AudioManager           = cache_class(env, "android/media/AudioManager");
    g_cls.AudioFormatBuilder     = cache_class(env, "android/media/AudioFormat$Builder");
    g_cls.PowerManager           = cache_class(env, "android/os/PowerManager");
    g_cls.WakeLock               = cache_class(env, "android/os/PowerManager$WakeLock");
    g_cls.NotificationManager    = cache_class(env, "android/app/NotificationManager");
    g_cls.NotificationChannel    = cache_class(env, "android/app/NotificationChannel");
    g_cls.NotificationBuilder    = cache_class(env, "android/app/Notification$Builder");
    g_cls.Intent                 = cache_class(env, "android/content/Intent");
    g_cls.PendingIntent          = cache_class(env, "android/app/PendingIntent");
    g_cls.Context                = cache_class(env, "android/content/Context");
    g_cls.Service                = cache_class(env, "android/app/Service");
    g_cls.DisplayManager         = cache_class(env, "android/hardware/display/DisplayManager");
    g_cls.Display                = cache_class(env, "android/view/Display");
    g_cls.DisplayMetrics         = cache_class(env, "android/util/DisplayMetrics");
    g_cls.WindowManager          = cache_class(env, "android/view/WindowManager");
    g_cls.Point                  = cache_class(env, "android/graphics/Point");
    g_cls.Rect                   = cache_class(env, "android/graphics/Rect");
    g_cls.Surface                = cache_class(env, "android/view/Surface");
    g_cls.ImageReader            = cache_class(env, "android/media/ImageReader");
    g_cls.AccessibilityService   = cache_class(env, "android/accessibilityservice/AccessibilityService");
    g_cls.AccessibilityEvent     = cache_class(env, "android/view/accessibility/AccessibilityEvent");
    g_cls.GestureDescription     = cache_class(env, "android/accessibilityservice/GestureDescription");
    g_cls.GestureStroke          = cache_class(env, "android/accessibilityservice/GestureDescription$StrokeDescription");
    g_cls.Path                   = cache_class(env, "android/graphics/Path");
    g_cls.MotionEvent            = cache_class(env, "android/view/MotionEvent");
    g_cls.KeyEvent               = cache_class(env, "android/view/KeyEvent");
    g_cls.InputManager           = cache_class(env, "android/hardware/input/InputManager");
    g_cls.Uri                    = cache_class(env, "android/net/Uri");
    g_cls.DocumentsContract      = cache_class(env, "android/provider/DocumentsContract");
    g_cls.ContentResolver        = cache_class(env, "android/content/ContentResolver");
    g_cls.DocumentFile           = cache_class(env, "androidx/documentfile/provider/DocumentFile");
    g_cls.File                   = cache_class(env, "java/io/File");
    g_cls.Activity               = cache_class(env, "android/app/Activity");
    g_cls.Bundle                 = cache_class(env, "android/os/Bundle");
    g_cls.Handler                = cache_class(env, "android/os/Handler");
    g_cls.Looper                 = cache_class(env, "android/os/Looper");
    g_cls.HandlerThread          = cache_class(env, "android/os/HandlerThread");
    g_cls.Executors              = cache_class(env, "java/util/concurrent/Executors");
    g_cls.CompletableFuture      = cache_class(env, "java/util/concurrent/CompletableFuture");
    g_cls.AtomicBoolean          = cache_class(env, "java/util/concurrent/atomic/AtomicBoolean");
    g_cls.Size                   = cache_class(env, "android/util/Size");
    g_cls.Configuration          = cache_class(env, "android/content/res/Configuration");
    g_cls.Resources              = cache_class(env, "android/content/res/Resources");
    g_cls.Build_VERSION          = cache_class(env, "android/os/Build$VERSION");
    g_cls.Build_VERSION_CODES    = cache_class(env, "android/os/Build$VERSION_CODES");

    g_cls.loaded = true;
    spdlog::info("[android] JNI class cache initialised ({} classes)",
                 (g_cls.MediaProjection ? 40 : 0));
}

// ---------------------------------------------------------------------------
//  JNI utility helpers
// ---------------------------------------------------------------------------

/** Delete a local reference, with null check. */
inline void safe_delete_local(JNIEnv *env, jobject obj) {
    if (obj) env->DeleteLocalRef(obj);
}

/** Convert a jstring to a std::string. */
inline std::string jstring_to_std(JNIEnv *env, jstring js) {
    if (!js) return {};
    const char *utf = env->GetStringUTFChars(js, nullptr);
    if (!utf) return {};
    std::string s(utf);
    env->ReleaseStringUTFChars(js, utf);
    return s;
}

/** Convert std::string to jstring. */
inline jstring std_to_jstring(JNIEnv *env, const std::string &s) {
    return env->NewStringUTF(s.c_str());
}

/** Get a static method ID, logging on failure. */
inline jmethodID get_static_method(JNIEnv *env, jclass cls,
                                    const char *name, const char *sig) {
    jmethodID mid = env->GetStaticMethodID(cls, name, sig);
    if (!mid) {
        ALOGE("GetStaticMethodID(%s, %s) failed", name, sig);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    return mid;
}

/** Get a method ID, logging on failure. */
inline jmethodID get_method(JNIEnv *env, jclass cls,
                             const char *name, const char *sig) {
    jmethodID mid = env->GetMethodID(cls, name, sig);
    if (!mid) {
        ALOGE("GetMethodID(%s, %s) failed", name, sig);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    return mid;
}

/** Get a field ID, logging on failure. */
inline jfieldID get_field(JNIEnv *env, jclass cls,
                           const char *name, const char *sig) {
    jfieldID fid = env->GetFieldID(cls, name, sig);
    if (!fid) {
        ALOGE("GetFieldID(%s, %s) failed", name, sig);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    return fid;
}

/** Get a static field ID. */
inline jfieldID get_static_field(JNIEnv *env, jclass cls,
                                  const char *name, const char *sig) {
    jfieldID fid = env->GetStaticFieldID(cls, name, sig);
    if (!fid) {
        ALOGE("GetStaticFieldID(%s, %s) failed", name, sig);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }
    return fid;
}

/** Convenience: call a static method that returns void. */
inline bool call_static_void(JNIEnv *env, jclass cls,
                              const char *name, const char *sig, ...) {
    jmethodID mid = get_static_method(env, cls, name, sig);
    if (!mid) return false;
    va_list args;
    va_start(args, sig);
    env->CallStaticVoidMethodV(cls, mid, args);
    va_end(args);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
    return true;
}

/** Convenience: call an object method that returns void. */
inline bool call_void(JNIEnv *env, jobject obj, jclass cls,
                       const char *name, const char *sig, ...) {
    jmethodID mid = get_method(env, cls, name, sig);
    if (!mid) return false;
    va_list args;
    va_start(args, sig);
    env->CallVoidMethodV(obj, mid, args);
    va_end(args);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
    return true;
}

/** Convenience: call an object method that returns jint. */
inline jint call_int(JNIEnv *env, jobject obj, jclass cls,
                      const char *name, const char *sig, ...) {
    jmethodID mid = get_method(env, cls, name, sig);
    if (!mid) return 0;
    va_list args;
    va_start(args, sig);
    jint r = env->CallIntMethodV(obj, mid, args);
    va_end(args);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return 0; }
    return r;
}

/** Convenience: call an object method that returns jlong. */
inline jlong call_long(JNIEnv *env, jobject obj, jclass cls,
                        const char *name, const char *sig, ...) {
    jmethodID mid = get_method(env, cls, name, sig);
    if (!mid) return 0;
    va_list args;
    va_start(args, sig);
    jlong r = env->CallLongMethodV(obj, mid, args);
    va_end(args);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return 0; }
    return r;
}

/** Convenience: call an object method that returns jboolean. */
inline jboolean call_bool(JNIEnv *env, jobject obj, jclass cls,
                           const char *name, const char *sig, ...) {
    jmethodID mid = get_method(env, cls, name, sig);
    if (!mid) return JNI_FALSE;
    va_list args;
    va_start(args, sig);
    jboolean r = env->CallBooleanMethodV(obj, mid, args);
    va_end(args);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return JNI_FALSE; }
    return r;
}

/** Convenience: call an object method that returns jobject. */
inline jobject call_object(JNIEnv *env, jobject obj, jclass cls,
                            const char *name, const char *sig, ...) {
    jmethodID mid = get_method(env, cls, name, sig);
    if (!mid) return nullptr;
    va_list args;
    va_start(args, sig);
    jobject r = env->CallObjectMethodV(obj, mid, args);
    va_end(args);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return nullptr; }
    return r;
}

/** Convenience: find a static int field value. */
inline jint get_static_int(JNIEnv *env, jclass cls, const char *name) {
    jfieldID fid = get_static_field(env, cls, name, "I");
    if (!fid) return 0;
    return env->GetStaticIntField(cls, fid);
}

/** Get Android SDK version at runtime. */
inline int android_sdk_version() {
    JniEnvGuard guard;
    if (!guard.env) return 21;
    jfieldID fid = get_static_field(guard.env, g_cls.Build_VERSION, "SDK_INT", "I");
    if (!fid) return 21;
    return guard.env->GetStaticIntField(g_cls.Build_VERSION, fid);
}

// ===========================================================================
//  SECTION 2 — MEDIA PROJECTION (Screen Capture)
// ===========================================================================

/**
 * Encapsulates a MediaProjection-based screen capture session.
 *
 * We use the Android MediaProjection API (API 21+) together with an
 * ImageReader or SurfaceTexture to grab frames from the display.
 *
 * Typical flow:
 *   1. User grants consent via createScreenCaptureIntent()
 *   2. The resulting Intent data is passed to getMediaProjection()
 *   3. createVirtualDisplay() projects onto an ImageReader Surface
 *   4. Frames arrive via the ImageReader.OnImageAvailableListener
 */
struct MediaProjectionCapture {
    // Java objects (global refs)
    jobject media_projection      = nullptr;   // MediaProjection
    jobject image_reader          = nullptr;   // ImageReader
    jobject virtual_display       = nullptr;   // VirtualDisplay (jobject)
    jobject display_surface       = nullptr;   // Surface from ImageReader
    jobject callback              = nullptr;   // ImageReader.OnImageAvailableListener

    // Capture configuration
    int width                     = 0;
    int height                    = 0;
    int density_dpi               = 320;
    int image_format              = 0x1;       // ImageFormat.YUV_420_888
    int max_images                = 2;

    // Thread synchronisation
    std::mutex frame_mutex;
    std::condition_variable frame_cv;
    std::deque<std::vector<uint8_t>> frame_buffer;
    std::atomic<bool> running{false};
    std::atomic<int64_t> frames_captured{0};
    std::atomic<int64_t> frames_dropped{0};

    // Methods
    bool initialise(JNIEnv *env, jobject ctx, jobject media_proj,
                    int w, int h, int dpi);
    void shutdown(JNIEnv *env);
    bool capture_frame(std::vector<uint8_t> &out);
    static void on_image_available(JNIEnv *env, jobject reader);
};

/**
 * Initialise a screen-capture session.
 *
 * @param env         JNI environment
 * @param ctx         Android Context (Activity or Service)
 * @param media_proj  MediaProjection object (from intent consent)
 * @param w, h        Desired capture width/height
 * @param dpi         Display density
 */
bool MediaProjectionCapture::initialise(JNIEnv *env, jobject ctx,
                                         jobject media_proj,
                                         int w, int h, int dpi) {
    if (!env || !ctx || !media_proj) {
        spdlog::error("[android] MediaProjectionCapture::initialise: null params");
        return false;
    }

    width = w;
    height = h;
    density_dpi = dpi;

    // Create a global ref to the MediaProjection
    media_projection = env->NewGlobalRef(media_proj);
    if (!media_projection) {
        spdlog::error("[android] Failed to create global ref for MediaProjection");
        return false;
    }

    // ---------- Create ImageReader ----------
    // ImageReader.newInstance(width, height, format, maxImages)
    jmethodID new_instance = get_static_method(env, g_cls.ImageReader,
                                                "newInstance", "(IIII)Landroid/media/ImageReader;");
    if (!new_instance) return false;

    jobject reader_local = env->CallStaticObjectMethod(
        g_cls.ImageReader, new_instance,
        static_cast<jint>(width),
        static_cast<jint>(height),
        static_cast<jint>(image_format),
        static_cast<jint>(max_images));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        spdlog::error("[android] ImageReader.newInstance failed");
        return false;
    }
    image_reader = env->NewGlobalRef(reader_local);
    env->DeleteLocalRef(reader_local);

    // Get the ImageReader Surface
    jmethodID get_surface = get_method(env, g_cls.ImageReader, "getSurface",
                                        "()Landroid/view/Surface;");
    jobject surf_local = env->CallObjectMethod(image_reader, get_surface);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
    display_surface = env->NewGlobalRef(surf_local);
    env->DeleteLocalRef(surf_local);

    // ---------- Create VirtualDisplay ----------
    // mediaProjection.createVirtualDisplay(name, width, height, dpi, flags,
    //      surface, callback, handler)
    jmethodID create_vd = get_method(env, g_cls.MediaProjection,
                                      "createVirtualDisplay",
                                      "(Ljava/lang/String;IIIILandroid/view/Surface;"
                                      "Landroid/hardware/display/VirtualDisplay$Callback;"
                                      "Landroid/os/Handler;)Landroid/hardware/display/VirtualDisplay;");
    if (!create_vd) return false;

    jstring name_j = std_to_jstring(env, "cppdesk-capture");
    // flags: VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR = 16 (API 27+)
    //         VIRTUAL_DISPLAY_FLAG_PUBLIC = 1
    constexpr jint vd_flags = 1;  // VIRTUAL_DISPLAY_FLAG_PUBLIC

    jobject vd_local = env->CallObjectMethod(
        media_projection, create_vd,
        name_j,
        static_cast<jint>(width),
        static_cast<jint>(height),
        static_cast<jint>(density_dpi),
        vd_flags,
        display_surface,
        nullptr,  // callback
        nullptr); // handler
    env->DeleteLocalRef(name_j);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        spdlog::error("[android] createVirtualDisplay failed");
        return false;
    }
    if (vd_local) {
        virtual_display = env->NewGlobalRef(vd_local);
        env->DeleteLocalRef(vd_local);
    }

    // ---------- Set OnImageAvailableListener ----------
    // We need a Java-side listener that calls back into native.
    // In practice this is done via a JNI callback object registered from
    // Java; here we define the pattern that would be used.
    //
    // ImageReader.setOnImageAvailableListener(listener, handler)
    jmethodID set_listener = get_method(env, g_cls.ImageReader,
                                         "setOnImageAvailableListener",
                                         "(Landroid/media/ImageReader$OnImageAvailableListener;"
                                         "Landroid/os/Handler;)V");
    if (!set_listener) return false;

    // For a full implementation, the listener object would be created in
    // Java (or via NewObject with a custom JNI class).  Here we supply
    // nullptr for the listener, meaning the Java layer must call a native
    // method when frames arrive.
    env->CallVoidMethod(image_reader, set_listener, nullptr, nullptr);
    if (env->ExceptionCheck()) env->ExceptionClear();

    running.store(true);
    spdlog::info("[android] MediaProjectionCapture initialised {}x{} @{}dpi",
                 width, height, density_dpi);
    return true;
}

void MediaProjectionCapture::shutdown(JNIEnv *env) {
    running.store(false);
    frame_cv.notify_all();

    if (env) {
        if (virtual_display) {
            call_void(env, virtual_display, nullptr,
                       "release", "()V");
            env->DeleteGlobalRef(virtual_display);
            virtual_display = nullptr;
        }
        if (display_surface) {
            env->DeleteGlobalRef(display_surface);
            display_surface = nullptr;
        }
        if (image_reader) {
            env->DeleteGlobalRef(image_reader);
            image_reader = nullptr;
        }
        if (media_projection) {
            call_void(env, media_projection, g_cls.MediaProjection,
                       "stop", "()V");
            env->DeleteGlobalRef(media_projection);
            media_projection = nullptr;
        }
    }

    spdlog::info("[android] MediaProjectionCapture shut down ({} frames, {} dropped)",
                 frames_captured.load(), frames_dropped.load());
}

/**
 * Grab the latest frame from the ImageReader.
 *
 * In a real implementation, the frames would be pulled from the
 * ImageReader inside the OnImageAvailableListener callback running
 * on a dedicated handler thread.
 */
bool MediaProjectionCapture::capture_frame(std::vector<uint8_t> &out) {
    std::unique_lock lock(frame_mutex);
    if (frame_buffer.empty()) {
        frame_cv.wait_for(lock, std::chrono::milliseconds(33));
    }
    if (frame_buffer.empty()) return false;
    out = std::move(frame_buffer.front());
    frame_buffer.pop_front();
    frames_captured.fetch_add(1);
    return true;
}

// ===========================================================================
//  SECTION 3 — MEDIACODEC HARDWARE ENCODER
// ===========================================================================

/**
 * Wraps the Android MediaCodec API for hardware-accelerated video encoding.
 *
 * Supports H.264 and H.265 (HEVC) encoders.
 */
struct MediaCodecEncoder {
    // Configuration
    std::string mime_type   = "video/avc";   // "video/hevc" for H.265
    int width               = 1920;
    int height              = 1080;
    int bitrate_bps         = 4'000'000;     // 4 Mbps
    int frame_rate          = 30;
    int i_frame_interval    = 1;             // I-frame every second
    int color_format        = 21;            // COLOR_FormatYUV420SemiPlanar

    // Java objects
    jobject codec           = nullptr;       // MediaCodec
    jobject input_buffers   = nullptr;       // ByteBuffer[] (deprecated but still used)
    jobject output_buffers  = nullptr;
    jobject buffer_info     = nullptr;       // MediaCodec.BufferInfo

    // State
    std::atomic<bool> running{false};
    std::atomic<int64_t> frames_encoded{0};
    std::mutex encoder_mutex;

    bool configure(JNIEnv *env);
    bool start(JNIEnv *env);
    void stop(JNIEnv *env);
    bool feed_frame(JNIEnv *env, const uint8_t *data, size_t len,
                    int64_t pts_us);
    bool drain_output(JNIEnv *env, std::vector<uint8_t> &encoded);
};

bool MediaCodecEncoder::configure(JNIEnv *env) {
    if (!env) return false;

    // ---------- Create MediaCodec by type ----------
    jmethodID create_by_type = get_static_method(
        env, g_cls.MediaCodec, "createEncoderByType",
        "(Ljava/lang/String;)Landroid/media/MediaCodec;");
    if (!create_by_type) return false;

    jstring mime = std_to_jstring(env, mime_type);
    jobject codec_local = env->CallStaticObjectMethod(g_cls.MediaCodec,
                                                       create_by_type, mime);
    env->DeleteLocalRef(mime);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        spdlog::error("[android] createEncoderByType({}) failed", mime_type);
        return false;
    }
    if (!codec_local) {
        spdlog::error("[android] No encoder found for {}", mime_type);
        return false;
    }
    codec = env->NewGlobalRef(codec_local);
    env->DeleteLocalRef(codec_local);

    // ---------- Create MediaFormat ----------
    jmethodID create_video_format = get_static_method(
        env, g_cls.MediaFormat, "createVideoFormat",
        "(Ljava/lang/String;II)Landroid/media/MediaFormat;");
    if (!create_video_format) return false;

    mime = std_to_jstring(env, mime_type);
    jobject format_local = env->CallStaticObjectMethod(
        g_cls.MediaFormat, create_video_format, mime,
        static_cast<jint>(width), static_cast<jint>(height));
    env->DeleteLocalRef(mime);

    if (!format_local) return false;

    // Set format parameters
    jmethodID set_int = get_method(env, g_cls.MediaFormat, "setInteger",
                                    "(Ljava/lang/String;I)V");
    jmethodID set_float = get_method(env, g_cls.MediaFormat, "setFloat",
                                      "(Ljava/lang/String;F)V");

    // KEY_BIT_RATE
    jstring key_br = std_to_jstring(env, "bitrate");
    env->CallVoidMethod(format_local, set_int, key_br, bitrate_bps);
    env->DeleteLocalRef(key_br);

    // KEY_FRAME_RATE
    jstring key_fr = std_to_jstring(env, "frame-rate");
    env->CallVoidMethod(format_local, set_int, key_fr, frame_rate);
    env->DeleteLocalRef(key_fr);

    // KEY_I_FRAME_INTERVAL
    jstring key_if = std_to_jstring(env, "i-frame-interval");
    env->CallVoidMethod(format_local, set_int, key_if, i_frame_interval);
    env->DeleteLocalRef(key_if);

    // KEY_COLOR_FORMAT
    jstring key_cf = std_to_jstring(env, "color-format");
    env->CallVoidMethod(format_local, set_int, key_cf, color_format);
    env->DeleteLocalRef(key_cf);

    // KEY_BITRATE_MODE — CBR (2) or VBR (1)
    jstring key_brm = std_to_jstring(env, "bitrate-mode");
    env->CallVoidMethod(format_local, set_int, key_brm, 2);
    env->DeleteLocalRef(key_brm);

    // KEY_MAX_B_FRAMES = 0 (for low-latency real-time)
    jstring key_bf = std_to_jstring(env, "max-bframes");
    env->CallVoidMethod(format_local, set_int, key_bf, 0);
    env->DeleteLocalRef(key_bf);

    // ---------- Configure the codec ----------
    jmethodID configure_mid = get_method(
        env, g_cls.MediaCodec, "configure",
        "(Landroid/media/MediaFormat;Landroid/view/Surface;"
        "Landroid/media/MediaCrypto;I)V");
    if (!configure_mid) return false;

    // CONFIGURE_FLAG_ENCODE = 1
    env->CallVoidMethod(codec, configure_mid, format_local, nullptr, nullptr, 1);
    env->DeleteLocalRef(format_local);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        spdlog::error("[android] MediaCodec.configure failed");
        return false;
    }

    spdlog::info("[android] MediaCodec {} configured: {}x{} @{}kbps",
                 mime_type, width, height, bitrate_bps / 1000);
    return true;
}

bool MediaCodecEncoder::start(JNIEnv *env) {
    if (!codec) return false;

    jmethodID start_mid = get_method(env, g_cls.MediaCodec, "start", "()V");
    if (!start_mid) return false;
    env->CallVoidMethod(codec, start_mid);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }

    running.store(true);
    spdlog::info("[android] MediaCodec encoder started");
    return true;
}

void MediaCodecEncoder::stop(JNIEnv *env) {
    running.store(false);
    if (!codec || !env) return;

    jmethodID stop_mid = get_method(env, g_cls.MediaCodec, "stop", "()V");
    jmethodID release_mid = get_method(env, g_cls.MediaCodec, "release", "()V");

    if (stop_mid) env->CallVoidMethod(codec, stop_mid);
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (release_mid) env->CallVoidMethod(codec, release_mid);
    if (env->ExceptionCheck()) env->ExceptionClear();

    env->DeleteGlobalRef(codec);
    codec = nullptr;
    spdlog::info("[android] MediaCodec encoder stopped ({} frames)",
                 frames_encoded.load());
}

/**
 * Feed a raw YUV frame to the encoder.
 *
 * @param data    Raw YUV/NV12 frame data
 * @param len     Data length in bytes
 * @param pts_us  Presentation timestamp in microseconds
 */
bool MediaCodecEncoder::feed_frame(JNIEnv *env, const uint8_t *data,
                                    size_t len, int64_t pts_us) {
    std::lock_guard lock(encoder_mutex);
    if (!running.load() || !codec) return false;

    // dequeueInputBuffer(timeoutUs)
    jmethodID dequeue_input = get_method(env, g_cls.MediaCodec,
                                          "dequeueInputBuffer", "(J)I");
    if (!dequeue_input) return false;

    jint input_idx = env->CallIntMethod(codec, dequeue_input, 10000LL);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
    if (input_idx < 0) {
        spdlog::warn("[android] dequeueInputBuffer returned {}", input_idx);
        return false;
    }

    // getInputBuffer(index) — returns ByteBuffer
    jmethodID get_input_buf = get_method(env, g_cls.MediaCodec,
                                          "getInputBuffer", "(I)Ljava/nio/ByteBuffer;");
    jobject buf = env->CallObjectMethod(codec, get_input_buf, input_idx);
    if (!buf) return false;

    // Get direct buffer address
    uint8_t *buf_ptr = static_cast<uint8_t *>(env->GetDirectBufferAddress(buf));
    jlong buf_cap = env->GetDirectBufferCapacity(buf);

    if (buf_ptr && static_cast<size_t>(buf_cap) >= len) {
        std::memcpy(buf_ptr, data, len);
    } else {
        env->DeleteLocalRef(buf);
        return false;
    }
    env->DeleteLocalRef(buf);

    // queueInputBuffer(index, offset, size, pts, flags)
    jmethodID queue_input = get_method(env, g_cls.MediaCodec,
                                        "queueInputBuffer", "(IIIJI)V");
    env->CallVoidMethod(codec, queue_input, input_idx,
                        0, static_cast<jint>(len), pts_us, 0);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }

    return true;
}

/**
 * Drain encoded output from the encoder.
 *
 * @return true if a complete encoded frame was retrieved.
 */
bool MediaCodecEncoder::drain_output(JNIEnv *env,
                                      std::vector<uint8_t> &encoded) {
    if (!running.load() || !codec) return false;

    // dequeueOutputBuffer(info, timeoutUs)
    jmethodID dequeue_output = get_method(env, g_cls.MediaCodec,
                                           "dequeueOutputBuffer",
                                           "(Landroid/media/MediaCodec$BufferInfo;J)I");
    if (!dequeue_output) return false;

    // Create BufferInfo object
    jmethodID bi_ctor = get_method(env, g_cls.BufferInfo, "<init>", "()V");
    jobject bi_local = env->NewObject(g_cls.BufferInfo, bi_ctor);

    jint output_idx = env->CallIntMethod(codec, dequeue_output, bi_local, 10000LL);
    if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(bi_local); return false; }

    if (output_idx < 0) {
        env->DeleteLocalRef(bi_local);
        return false; // No output available yet
    }

    // Check for end-of-stream
    constexpr jint INFO_OUTPUT_FORMAT_CHANGED = -2;
    constexpr jint INFO_TRY_AGAIN_LATER       = -1;

    if (output_idx == INFO_OUTPUT_FORMAT_CHANGED) {
        env->DeleteLocalRef(bi_local);
        return false; // format changed — handled separately
    }

    // Read BufferInfo fields
    jfieldID fid_offset = get_field(env, g_cls.BufferInfo, "offset", "I");
    jfieldID fid_size   = get_field(env, g_cls.BufferInfo, "size", "I");
    jfieldID fid_pts    = get_field(env, g_cls.BufferInfo, "presentationTimeUs", "J");
    jfieldID fid_flags  = get_field(env, g_cls.BufferInfo, "flags", "I");

    jint offset = env->GetIntField(bi_local, fid_offset);
    jint size   = env->GetIntField(bi_local, fid_size);
    jint flags  = env->GetIntField(bi_local, fid_flags);

    if (size > 0) {
        jmethodID get_output_buf = get_method(env, g_cls.MediaCodec,
                                               "getOutputBuffer", "(I)Ljava/nio/ByteBuffer;");
        jobject buf = env->CallObjectMethod(codec, get_output_buf, output_idx);
        if (buf) {
            uint8_t *buf_ptr = static_cast<uint8_t *>(env->GetDirectBufferAddress(buf))
                               + offset;
            encoded.assign(buf_ptr, buf_ptr + size);
            env->DeleteLocalRef(buf);
            frames_encoded.fetch_add(1);
        }
    }

    // releaseOutputBuffer(index, render)
    jmethodID release_output = get_method(env, g_cls.MediaCodec,
                                           "releaseOutputBuffer", "(IZ)V");
    env->CallVoidMethod(codec, release_output, output_idx, JNI_FALSE);

    env->DeleteLocalRef(bi_local);
    if (env->ExceptionCheck()) env->ExceptionClear();
    return size > 0;
}

// ===========================================================================
//  SECTION 4 — AudioRecord (Microphone Capture)
// ===========================================================================

/**
 * Captures audio from the device microphone using Android AudioRecord.
 *
 * Configurable sample rate, channel count, and encoding.
 */
struct AudioCapturer {
    // Audio parameters
    int sample_rate          = 48000;
    int channel_count        = 1;       // mono
    int audio_format         = 2;       // ENCODING_PCM_16BIT
    int audio_source         = 6;       // VOICE_COMMUNICATION (echo cancellation)
    int buffer_size_bytes    = 0;

    // Java object
    jobject audio_record     = nullptr;
    std::vector<int16_t> capture_buffer;  // native-side ring buffer

    // State
    std::atomic<bool> running{false};
    std::mutex capture_mutex;
    std::thread capture_thread;

    bool initialise(JNIEnv *env);
    void start_capture(std::function<void(const int16_t *, size_t)> callback);
    void stop();
    size_t available_frames() const;
};

bool AudioCapturer::initialise(JNIEnv *env) {
    if (!env) return false;

    // Compute minimum buffer size
    jmethodID get_min_buf = get_static_method(
        env, g_cls.AudioRecord, "getMinBufferSize",
        "(III)I");
    if (!get_min_buf) return false;

    jint min_buf = env->CallStaticIntMethod(
        g_cls.AudioRecord, get_min_buf,
        sample_rate,
        channel_count == 1 ? 0x10 : 0xC, // CHANNEL_IN_MONO : CHANNEL_IN_STEREO
        audio_format);
    if (min_buf <= 0) {
        spdlog::error("[android] getMinBufferSize returned {}", min_buf);
        return false;
    }
    buffer_size_bytes = min_buf * 2;  // double for safety

    // Construct AudioRecord
    jmethodID ctor = get_method(env, g_cls.AudioRecord, "<init>",
                                 "(IIIII)V");
    jobject ar_local = env->NewObject(g_cls.AudioRecord, ctor,
                                       audio_source, sample_rate,
                                       channel_count == 1 ? 0x10 : 0xC,
                                       audio_format, buffer_size_bytes);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        spdlog::error("[android] AudioRecord constructor failed");
        return false;
    }

    // Check state
    jmethodID get_state = get_method(env, g_cls.AudioRecord,
                                      "getState", "()I");
    jint state = env->CallIntMethod(ar_local, get_state);
    if (state != 1) { // STATE_INITIALIZED
        env->DeleteLocalRef(ar_local);
        spdlog::error("[android] AudioRecord not initialised (state={})", state);
        return false;
    }

    audio_record = env->NewGlobalRef(ar_local);
    env->DeleteLocalRef(ar_local);

    capture_buffer.resize(buffer_size_bytes / sizeof(int16_t));

    spdlog::info("[android] AudioRecord initialised: {}Hz {}ch, buf={}B",
                 sample_rate, channel_count, buffer_size_bytes);
    return true;
}

void AudioCapturer::start_capture(
    std::function<void(const int16_t *, size_t)> callback) {
    if (!audio_record) return;

    JniEnvGuard guard;
    if (!guard.env) return;

    // startRecording()
    jmethodID start_rec = get_method(guard.env, g_cls.AudioRecord,
                                      "startRecording", "()V");
    guard.env->CallVoidMethod(audio_record, start_rec);
    if (guard.env->ExceptionCheck()) {
        guard.env->ExceptionClear();
        return;
    }

    running.store(true);

    // Launch capture thread
    capture_thread = std::thread([this, cb = std::move(callback)]() {
        JniEnvGuard t_guard;
        if (!t_guard.env) return;

        jmethodID read_mid = get_method(t_guard.env, g_cls.AudioRecord,
                                         "read", "([SII)I");

        std::vector<int16_t> local_buf(buffer_size_bytes / sizeof(int16_t));

        while (running.load()) {
            jshortArray jbuf = t_guard.env->NewShortArray(
                static_cast<jsize>(local_buf.size()));
            if (!jbuf) break;

            jint frames = t_guard.env->CallIntMethod(
                audio_record, read_mid,
                jbuf, 0, static_cast<jint>(local_buf.size()));

            if (frames > 0) {
                t_guard.env->GetShortArrayRegion(
                    jbuf, 0, frames,
                    reinterpret_cast<jshort *>(local_buf.data()));
                cb(local_buf.data(), static_cast<size_t>(frames));
            } else if (frames < 0) {
                // Error — break out
                spdlog::warn("[android] AudioRecord read error: {}", frames);
                break;
            }

            t_guard.env->DeleteLocalRef(jbuf);
        }
    });

    spdlog::info("[android] Audio capture started");
}

void AudioCapturer::stop() {
    running.store(false);
    if (capture_thread.joinable()) {
        capture_thread.join();
    }

    JniEnvGuard guard;
    if (guard.env && audio_record) {
        call_void(guard.env, audio_record, g_cls.AudioRecord,
                   "stop", "()V");
        call_void(guard.env, audio_record, g_cls.AudioRecord,
                   "release", "()V");
        guard.env->DeleteGlobalRef(audio_record);
        audio_record = nullptr;
    }
    spdlog::info("[android] Audio capture stopped");
}

// ===========================================================================
//  SECTION 5 — AudioTrack (Audio Playback)
// ===========================================================================

/**
 * Plays received audio through the device speaker/headset using AudioTrack.
 */
struct AudioPlayer {
    int sample_rate         = 48000;
    int channel_count       = 1;
    int audio_format        = 2;   // ENCODING_PCM_16BIT
    int stream_type         = 0;   // STREAM_VOICE_CALL
    int usage               = 2;   // USAGE_VOICE_COMMUNICATION
    int buffer_size_bytes   = 0;

    jobject audio_track     = nullptr;
    std::atomic<bool> running{false};

    bool initialise(JNIEnv *env);
    bool write(const int16_t *data, size_t frames);
    void start(JNIEnv *env);
    void stop(JNIEnv *env);
};

bool AudioPlayer::initialise(JNIEnv *env) {
    if (!env) return false;

    // Get minimum buffer size
    jmethodID get_min_buf = get_static_method(
        env, g_cls.AudioTrack, "getMinBufferSize",
        "(III)I");
    jint min_buf = env->CallStaticIntMethod(
        g_cls.AudioTrack, get_min_buf,
        sample_rate,
        channel_count == 1 ? 0x4 : 0xC, // CHANNEL_OUT_MONO : CHANNEL_OUT_STEREO
        audio_format);
    buffer_size_bytes = min_buf * 2;

    // For API 23+, use AudioTrack.Builder
    if (android_sdk_version() >= 23) {
        jclass builder_cls = cache_class(env, "android/media/AudioTrack$Builder");
        if (builder_cls) {
            jmethodID builder_ctor = get_method(env, builder_cls, "<init>", "()V");
            jobject builder = env->NewObject(builder_cls, builder_ctor);

            // Set attributes via builder
            jmethodID set_af = get_method(env, builder_cls, "setAudioAttributes",
                                           "(Landroid/media/AudioAttributes;)"
                                           "Landroid/media/AudioTrack$Builder;");
            // ... (in practice, build AudioAttributes and set)

            jmethodID set_fmt = get_method(env, builder_cls, "setAudioFormat",
                                            "(Landroid/media/AudioFormat;)"
                                            "Landroid/media/AudioTrack$Builder;");

            jmethodID build_mid = get_method(env, builder_cls, "build",
                                              "()Landroid/media/AudioTrack;");
            jobject at_local = env->CallObjectMethod(builder, build_mid);

            if (at_local && !env->ExceptionCheck()) {
                audio_track = env->NewGlobalRef(at_local);
                env->DeleteLocalRef(at_local);
            }
            env->DeleteLocalRef(builder);
            env->DeleteLocalRef(builder_cls);
        }
    }

    // Fallback to deprecated constructor
    if (!audio_track) {
        jmethodID ctor = get_method(env, g_cls.AudioTrack, "<init>",
                                     "(IIIIII)V");
        jobject at_local = env->NewObject(g_cls.AudioTrack, ctor,
                                           stream_type, sample_rate,
                                           channel_count == 1 ? 0x4 : 0xC,
                                           audio_format,
                                           buffer_size_bytes,
                                           1); // MODE_STREAM
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return false;
        }
        audio_track = env->NewGlobalRef(at_local);
        env->DeleteLocalRef(at_local);
    }

    spdlog::info("[android] AudioTrack initialised: {}Hz {}ch, buf={}B",
                 sample_rate, channel_count, buffer_size_bytes);
    return true;
}

void AudioPlayer::start(JNIEnv *env) {
    if (!audio_track) return;
    jmethodID play_mid = get_method(env, g_cls.AudioTrack, "play", "()V");
    env->CallVoidMethod(audio_track, play_mid);
    if (env->ExceptionCheck()) env->ExceptionClear();
    running.store(true);
    spdlog::info("[android] Audio playback started");
}

bool AudioPlayer::write(const int16_t *data, size_t frames) {
    if (!running.load() || !audio_track) return false;

    JniEnvGuard guard;
    if (!guard.env) return false;

    jmethodID write_mid = get_method(guard.env, g_cls.AudioTrack,
                                      "write", "([SII)I");
    jshortArray jbuf = guard.env->NewShortArray(static_cast<jsize>(frames));
    guard.env->SetShortArrayRegion(jbuf, 0,
                                    static_cast<jsize>(frames),
                                    reinterpret_cast<const jshort *>(data));
    jint written = guard.env->CallIntMethod(audio_track, write_mid,
                                             jbuf, 0, static_cast<jint>(frames));
    guard.env->DeleteLocalRef(jbuf);

    return written > 0;
}

void AudioPlayer::stop(JNIEnv *env) {
    running.store(false);
    if (!audio_track || !env) return;
    call_void(env, audio_track, g_cls.AudioTrack, "stop", "()V");
    call_void(env, audio_track, g_cls.AudioTrack, "release", "()V");
    env->DeleteGlobalRef(audio_track);
    audio_track = nullptr;
    spdlog::info("[android] Audio playback stopped");
}

// ===========================================================================
//  SECTION 6 — INPUT INJECTION (AccessibilityService)
// ===========================================================================

/**
 * Injects touch, gesture, and key events via AccessibilityService.
 *
 * On Android, input injection at the system level requires either:
 *   - AccessibilityService (dispatchGesture, performGlobalAction)
 *   - INJECT_EVENTS permission (system apps only)
 *   - InputManager.injectInputEvent (signature-level permission)
 *
 * This implementation targets AccessibilityService as the most
 * portable approach for non-system apps.
 */
struct InputInjector {
    jobject accessibility_service = nullptr;  // AccessibilityService instance

    // Gesture constants (from AccessibilityService)
    static constexpr int GESTURE_SWIPE_UP    = 1;
    static constexpr int GESTURE_SWIPE_DOWN  = 2;
    static constexpr int GESTURE_SWIPE_LEFT  = 3;
    static constexpr int GESTURE_SWIPE_RIGHT = 4;

    // Global action constants
    static constexpr int GLOBAL_ACTION_BACK           = 1;
    static constexpr int GLOBAL_ACTION_HOME           = 2;
    static constexpr int GLOBAL_ACTION_RECENTS        = 3;
    static constexpr int GLOBAL_ACTION_NOTIFICATIONS  = 4;
    static constexpr int GLOBAL_ACTION_QUICK_SETTINGS = 5;
    static constexpr int GLOBAL_ACTION_POWER_DIALOG   = 6;

    void set_service(JNIEnv *env, jobject service);
    bool dispatch_gesture(JNIEnv *env,
                          const std::vector<std::pair<float, float>> &path,
                          int64_t duration_ms);
    bool perform_global_action(JNIEnv *env, int action);
    bool inject_tap(JNIEnv *env, float x, float y);
    bool inject_swipe(JNIEnv *env, float x1, float y1, float x2, float y2,
                      int64_t duration_ms);
    bool inject_key_event(JNIEnv *env, int keycode, bool down);
    bool inject_text(JNIEnv *env, const std::string &text);
};

void InputInjector::set_service(JNIEnv *env, jobject service) {
    if (accessibility_service) env->DeleteGlobalRef(accessibility_service);
    accessibility_service = service ? env->NewGlobalRef(service) : nullptr;
}

bool InputInjector::dispatch_gesture(
    JNIEnv *env,
    const std::vector<std::pair<float, float>> &path,
    int64_t duration_ms) {
    if (!accessibility_service || path.empty()) return false;

    // Build a Path object
    jmethodID path_ctor = get_method(env, g_cls.Path, "<init>", "()V");
    jobject path_obj = env->NewObject(g_cls.Path, path_ctor);

    jmethodID move_to = get_method(env, g_cls.Path, "moveTo", "(FF)V");
    jmethodID line_to = get_method(env, g_cls.Path, "lineTo", "(FF)V");

    // Starting point
    env->CallVoidMethod(path_obj, move_to, path[0].first, path[0].second);

    for (size_t i = 1; i < path.size(); ++i) {
        env->CallVoidMethod(path_obj, line_to, path[i].first, path[i].second);
    }

    // Create StrokeDescription
    jmethodID stroke_ctor = get_method(
        env, g_cls.GestureStroke, "<init>",
        "(Landroid/graphics/Path;JJ)V");
    jobject stroke = env->NewObject(g_cls.GestureStroke, stroke_ctor,
                                     path_obj,
                                     0LL,  // start time
                                     duration_ms);
    env->DeleteLocalRef(path_obj);

    // Create GestureDescription.Builder
    jclass gd_builder_cls = cache_class(
        env, "android/accessibilityservice/GestureDescription$Builder");
    jmethodID b_ctor = get_method(env, gd_builder_cls, "<init>", "()V");
    jobject builder = env->NewObject(gd_builder_cls, b_ctor);

    jmethodID add_stroke = get_method(
        env, gd_builder_cls, "addStroke",
        "(Landroid/accessibilityservice/GestureDescription$StrokeDescription;)"
        "Landroid/accessibilityservice/GestureDescription$Builder;");
    env->CallObjectMethod(builder, add_stroke, stroke);
    env->DeleteLocalRef(stroke);

    jmethodID build_gd = get_method(
        env, gd_builder_cls, "build",
        "()Landroid/accessibilityservice/GestureDescription;");
    jobject gesture_desc = env->CallObjectMethod(builder, build_gd);
    env->DeleteLocalRef(builder);
    env->DeleteLocalRef(gd_builder_cls);

    // dispatchGesture(description, callback, handler)
    jmethodID dispatch = get_method(
        env, g_cls.AccessibilityService, "dispatchGesture",
        "(Landroid/accessibilityservice/GestureDescription;"
        "Landroid/accessibilityservice/AccessibilityService$GestureResultCallback;"
        "Landroid/os/Handler;)Z");
    jboolean dispatched = env->CallBooleanMethod(
        accessibility_service, dispatch, gesture_desc, nullptr, nullptr);
    env->DeleteLocalRef(gesture_desc);

    if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
    return dispatched == JNI_TRUE;
}

bool InputInjector::perform_global_action(JNIEnv *env, int action) {
    if (!accessibility_service) return false;
    jmethodID pga = get_method(env, g_cls.AccessibilityService,
                                "performGlobalAction", "(I)Z");
    jboolean ok = env->CallBooleanMethod(accessibility_service, pga, action);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
    return ok == JNI_TRUE;
}

bool InputInjector::inject_tap(JNIEnv *env, float x, float y) {
    return dispatch_gesture(env, {{x, y}}, 100);
}

bool InputInjector::inject_swipe(JNIEnv *env, float x1, float y1,
                                  float x2, float y2, int64_t duration_ms) {
    return dispatch_gesture(env, {{x1, y1}, {x2, y2}}, duration_ms);
}

/**
 * Inject a key event using the AccessibilityService node tree.
 *
 * Full key injection at the raw level requires INJECT_EVENTS permission;
 * here we use AccessibilityNodeInfo.performAction(ACTION_SET_TEXT)
 * for text input, and performGlobalAction for navigation keys.
 */
bool InputInjector::inject_key_event(JNIEnv *env, int keycode, bool down) {
    // Map common keycodes to global actions
    switch (keycode) {
    case 4:   return perform_global_action(env, GLOBAL_ACTION_BACK);
    case 3:   return perform_global_action(env, GLOBAL_ACTION_HOME);
    case 187: return perform_global_action(env, GLOBAL_ACTION_RECENTS);
    default:
        spdlog::warn("[android] Key injection for keycode {} not supported via AccessibilityService",
                     keycode);
        return false;
    }
}

bool InputInjector::inject_text(JNIEnv *env, const std::string &text) {
    if (!accessibility_service) return false;

    // getRootInActiveWindow() → AccessibilityNodeInfo
    jmethodID get_root = get_method(env, g_cls.AccessibilityService,
                                     "getRootInActiveWindow",
                                     "()Landroid/view/accessibility/AccessibilityNodeInfo;");
    jobject root = env->CallObjectMethod(accessibility_service, get_root);
    if (!root) return false;

    // findFocus(AccessibilityNodeInfo.FOCUS_INPUT)
    jclass node_info_cls = cache_class(
        env, "android/view/accessibility/AccessibilityNodeInfo");
    jmethodID find_focus = get_method(env, node_info_cls, "findFocus",
                                       "(I)Landroid/view/accessibility/AccessibilityNodeInfo;");
    jobject focused = env->CallObjectMethod(root, find_focus, 1); // FOCUS_INPUT
    env->DeleteLocalRef(root);

    if (!focused) {
        // Fallback: try to find the first editable node
        env->DeleteLocalRef(node_info_cls);
        return false;
    }

    // ACTION_SET_TEXT = 0x200000
    jmethodID perform_action = get_method(env, node_info_cls,
                                           "performAction", "(I)Z");
    jclass charseq_cls = env->FindClass("java/lang/CharSequence");

    // Build a Bundle with ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE
    //     "android.view.accessibility.action.ARGUMENT_SET_TEXT_CHARSEQUENCE"
    jclass bundle_cls = cache_class(env, "android/os/Bundle");
    jmethodID bundle_ctor = get_method(env, bundle_cls, "<init>", "()V");
    jmethodID put_cs = get_method(env, bundle_cls, "putCharSequence",
                                   "(Ljava/lang/String;Ljava/lang/CharSequence;)V");
    jmethodID perform_action_bundle = get_method(
        env, node_info_cls, "performAction",
        "(ILandroid/os/Bundle;)Z");

    jobject bundle = env->NewObject(bundle_cls, bundle_ctor);
    jstring key = std_to_jstring(
        env, "ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE");
    jstring value = std_to_jstring(env, text);
    env->CallVoidMethod(bundle, put_cs, key, value);
    env->DeleteLocalRef(key);
    env->DeleteLocalRef(value);

    jboolean ok = env->CallBooleanMethod(focused, perform_action_bundle,
                                          0x200000, bundle);
    env->DeleteLocalRef(bundle);
    env->DeleteLocalRef(focused);
    env->DeleteLocalRef(node_info_cls);
    env->DeleteLocalRef(bundle_cls);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }

    return ok == JNI_TRUE;
}

// ===========================================================================
//  SECTION 7 — FOREGROUND SERVICE
// ===========================================================================

/**
 * Manages the Android foreground service that keeps cppdesk alive
 * in the background.
 *
 * Android 8+ (API 26) requires foreground services with a persistent
 * notification.  Android 14+ (API 34) requires explicit foreground
 * service types.
 */
struct ForegroundService {
    jobject service_instance  = nullptr;  // The running Service object
    int     notification_id   = 0x434444; // "CPDSK" in hex-ish

    // Foreground service type constants (API 29+)
    static constexpr int FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION = 0x200;
    static constexpr int FOREGROUND_SERVICE_TYPE_MICROPHONE       = 0x80;
    static constexpr int FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE = 0x10;
    static constexpr int FOREGROUND_SERVICE_TYPE_REMOTE_MESSAGING = 0x100;
    static constexpr int FOREGROUND_SERVICE_TYPE_SPECIAL_USE      = 0x40000000;

    // Default combined types for a remote-desktop session
    int foreground_service_types =
        FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION |
        FOREGROUND_SERVICE_TYPE_MICROPHONE |
        FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE;

    bool start_foreground(JNIEnv *env, jobject service,
                          jobject notification);
    void stop_foreground(JNIEnv *env, bool remove_notification = true);
    bool update_notification(JNIEnv *env, jobject notification);
};

bool ForegroundService::start_foreground(JNIEnv *env, jobject service,
                                          jobject notification) {
    if (!env || !service || !notification) return false;
    if (service_instance) env->DeleteGlobalRef(service_instance);
    service_instance = env->NewGlobalRef(service);

    int sdk = android_sdk_version();

    if (sdk >= 29) {
        // startForeground(id, notification, foregroundServiceType)
        jmethodID start_fg = get_method(env, g_cls.Service,
                                         "startForeground",
                                         "(ILandroid/app/Notification;I)V");
        if (start_fg) {
            env->CallVoidMethod(service_instance, start_fg,
                                notification_id, notification,
                                foreground_service_types);
            if (!env->ExceptionCheck()) {
                spdlog::info("[android] Foreground service started (API 29+, types=0x{:x})",
                             foreground_service_types);
                return true;
            }
            env->ExceptionClear();
        }
    }

    // Fallback: API 26–28
    jmethodID start_fg = get_method(env, g_cls.Service,
                                     "startForeground",
                                     "(ILandroid/app/Notification;)V");
    env->CallVoidMethod(service_instance, start_fg,
                         notification_id, notification);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        spdlog::error("[android] startForeground failed");
        return false;
    }

    spdlog::info("[android] Foreground service started (pre-29 API)");
    return true;
}

void ForegroundService::stop_foreground(JNIEnv *env, bool remove_notification) {
    if (!service_instance || !env) return;

    jmethodID stop_fg = get_method(env, g_cls.Service,
                                    "stopForeground", "(Z)V");
    env->CallVoidMethod(service_instance, stop_fg,
                         remove_notification ? JNI_TRUE : JNI_FALSE);
    if (env->ExceptionCheck()) env->ExceptionClear();

    env->DeleteGlobalRef(service_instance);
    service_instance = nullptr;
    spdlog::info("[android] Foreground service stopped");
}

bool ForegroundService::update_notification(JNIEnv *env,
                                             jobject notification) {
    if (!service_instance || !env) return false;
    jclass nm_cls = cache_class(env, "android/app/NotificationManager");
    jobject nm = call_object(env, service_instance, g_cls.Service,
                              "getSystemService",
                              "(Ljava/lang/String;)Ljava/lang/Object;",
                              std_to_jstring(env, "notification"));
    if (!nm) return false;

    jmethodID notify = get_method(env, nm_cls, "notify", "(ILandroid/app/Notification;)V");
    env->CallVoidMethod(nm, notify, notification_id, notification);
    env->DeleteLocalRef(nm);
    env->DeleteLocalRef(nm_cls);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
    return true;
}

// ===========================================================================
//  SECTION 8 — WAKELOCK MANAGEMENT
// ===========================================================================

/**
 * Acquires and releases PowerManager.WakeLock to keep the CPU awake
 * during an active remote session.
 *
 * WakeLock types used:
 *   - PARTIAL_WAKE_LOCK  (CPU on, screen may be off) — default
 *   - FULL_WAKE_LOCK     (deprecated, API 17+)
 *   - PROXIMITY_SCREEN_OFF_WAKE_LOCK
 */
struct WakeLockManager {
    jobject power_manager    = nullptr;
    jobject wake_lock        = nullptr;
    bool    acquired         = false;
    int     wake_lock_level  = 0x1; // PARTIAL_WAKE_LOCK

    static constexpr int PARTIAL_WAKE_LOCK           = 0x1;
    static constexpr int FULL_WAKE_LOCK              = 0x1A; // deprecated
    static constexpr int SCREEN_DIM_WAKE_LOCK        = 0x6;
    static constexpr int SCREEN_BRIGHT_WAKE_LOCK     = 0xA;
    static constexpr int PROXIMITY_SCREEN_OFF_WAKE_LOCK = 0x20;

    bool acquire(JNIEnv *env, jobject context, bool keep_screen_on = false);
    void release(JNIEnv *env);
    bool is_held() const { return acquired; }
};

bool WakeLockManager::acquire(JNIEnv *env, jobject context,
                               bool keep_screen_on) {
    if (!env || !context) return false;
    if (acquired) return true;  // already held

    // Get PowerManager
    jmethodID get_sys_svc = get_method(env, g_cls.Context,
                                        "getSystemService",
                                        "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring power_svc = std_to_jstring(env, "power");
    jobject pm_local = env->CallObjectMethod(context, get_sys_svc, power_svc);
    env->DeleteLocalRef(power_svc);
    if (!pm_local) return false;

    power_manager = env->NewGlobalRef(pm_local);
    env->DeleteLocalRef(pm_local);

    // Build a wake-lock tag
    jstring tag = std_to_jstring(env, "cppdesk:wakelock");

    int level = keep_screen_on ? SCREEN_DIM_WAKE_LOCK : PARTIAL_WAKE_LOCK;

    // PowerManager.newWakeLock(levelAndFlags, tag)
    jmethodID new_wl = get_method(env, g_cls.PowerManager,
                                   "newWakeLock",
                                   "(ILjava/lang/String;)"
                                   "Landroid/os/PowerManager$WakeLock;");
    jobject wl_local = env->CallObjectMethod(power_manager, new_wl,
                                              level, tag);
    env->DeleteLocalRef(tag);
    if (!wl_local) return false;

    wake_lock = env->NewGlobalRef(wl_local);
    env->DeleteLocalRef(wl_local);

    // Set reference counted?  No — we want explicit acquire/release
    jmethodID set_ref = get_method(env, g_cls.WakeLock,
                                    "setReferenceCounted", "(Z)V");
    if (set_ref) {
        env->CallVoidMethod(wake_lock, set_ref, JNI_FALSE);
    }

    // acquire()
    jmethodID acquire_mid = get_method(env, g_cls.WakeLock,
                                        "acquire", "()V");
    // Add a timeout — 10 minutes max, then auto-release
    jmethodID acquire_timed = get_method(env, g_cls.WakeLock,
                                          "acquire", "(J)V");
    if (acquire_timed) {
        env->CallVoidMethod(wake_lock, acquire_timed,
                             static_cast<jlong>(10 * 60 * 1000));
    } else if (acquire_mid) {
        env->CallVoidMethod(wake_lock, acquire_mid);
    }

    acquired = true;
    spdlog::info("[android] WakeLock acquired (level={}, screen={})",
                 level, keep_screen_on);
    return true;
}

void WakeLockManager::release(JNIEnv *env) {
    if (!acquired || !wake_lock) return;

    // Check if held
    jmethodID is_held = get_method(env, g_cls.WakeLock, "isHeld", "()Z");
    jboolean held = env->CallBooleanMethod(wake_lock, is_held);
    if (held) {
        jmethodID release_mid = get_method(env, g_cls.WakeLock,
                                            "release", "()V");
        env->CallVoidMethod(wake_lock, release_mid);
    }

    env->DeleteGlobalRef(wake_lock);
    wake_lock = nullptr;

    if (power_manager) {
        env->DeleteGlobalRef(power_manager);
        power_manager = nullptr;
    }

    acquired = false;
    spdlog::info("[android] WakeLock released");
}

// ===========================================================================
//  SECTION 9 — NOTIFICATION CHANNEL SETUP
// ===========================================================================

/**
 * Sets up Android notification channels (required for API 26+)
 * and builds foreground-service notifications.
 */
struct NotificationHelper {
    static constexpr const char *CHANNEL_ID_SERVICE  = "cppdesk_service";
    static constexpr const char *CHANNEL_NAME_SERVICE = "Remote Session";
    static constexpr const char *CHANNEL_ID_ALERTS   = "cppdesk_alerts";
    static constexpr const char *CHANNEL_NAME_ALERTS  = "Alerts";

    int importance_service = 2;  // IMPORTANCE_LOW  (no sound)
    int importance_alerts  = 4;  // IMPORTANCE_HIGH (sound + heads-up)

    void create_channels(JNIEnv *env, jobject context);
    jobject build_service_notification(JNIEnv *env, jobject context,
                                        const std::string &title,
                                        const std::string &text);
    jobject build_alert_notification(JNIEnv *env, jobject context,
                                      const std::string &title,
                                      const std::string &text);
};

void NotificationHelper::create_channels(JNIEnv *env, jobject context) {
    if (android_sdk_version() < 26) return;  // channels introduced in API 26

    jmethodID get_sys_svc = get_method(env, g_cls.Context,
                                        "getSystemService",
                                        "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring ns = std_to_jstring(env, "notification");
    jobject nm_local = env->CallObjectMethod(context, get_sys_svc, ns);
    env->DeleteLocalRef(ns);
    if (!nm_local) return;

    jclass nm_cls = env->GetObjectClass(nm_local);
    // or use cache_class("android/app/NotificationManager")

    // Create service channel
    jmethodID nc_ctor = get_method(env, g_cls.NotificationChannel, "<init>",
                                    "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;I)V");
    jstring ch_id = std_to_jstring(env, CHANNEL_ID_SERVICE);
    jstring ch_name = std_to_jstring(env, CHANNEL_NAME_SERVICE);

    jobject ch_local = env->NewObject(g_cls.NotificationChannel, nc_ctor,
                                       ch_id, ch_name, importance_service);
    if (ch_local) {
        // Disable sound & vibration for the service channel
        jmethodID set_sound = get_method(env, g_cls.NotificationChannel,
                                          "setSound",
                                          "(Landroid/net/Uri;Landroid/media/AudioAttributes;)V");
        env->CallVoidMethod(ch_local, set_sound, nullptr, nullptr);

        jmethodID create_ch = get_method(env, nm_cls,
                                          "createNotificationChannel", "(Landroid/app/NotificationChannel;)V");
        env->CallVoidMethod(nm_local, create_ch, ch_local);
        env->DeleteLocalRef(ch_local);
    }
    env->DeleteLocalRef(ch_id);
    env->DeleteLocalRef(ch_name);

    // Create alerts channel
    ch_id = std_to_jstring(env, CHANNEL_ID_ALERTS);
    ch_name = std_to_jstring(env, CHANNEL_NAME_ALERTS);
    ch_local = env->NewObject(g_cls.NotificationChannel, nc_ctor,
                               ch_id, ch_name, importance_alerts);
    if (ch_local) {
        jmethodID create_ch = get_method(env, nm_cls,
                                          "createNotificationChannel", "(Landroid/app/NotificationChannel;)V");
        env->CallVoidMethod(nm_local, create_ch, ch_local);
        env->DeleteLocalRef(ch_local);
    }
    env->DeleteLocalRef(ch_id);
    env->DeleteLocalRef(ch_name);

    env->DeleteLocalRef(nm_local);
    env->DeleteLocalRef(nm_cls);

    spdlog::info("[android] Notification channels created");
}

jobject NotificationHelper::build_service_notification(
    JNIEnv *env, jobject context,
    const std::string &title, const std::string &text) {
    if (!context) return nullptr;

    // Notification.Builder(context, channelId)
    jmethodID builder_ctor = get_method(env, g_cls.NotificationBuilder,
                                         "<init>",
                                         "(Landroid/content/Context;Ljava/lang/String;)V");
    jstring ch_id = std_to_jstring(env, CHANNEL_ID_SERVICE);
    jobject builder = env->NewObject(g_cls.NotificationBuilder, builder_ctor,
                                      context, ch_id);
    env->DeleteLocalRef(ch_id);
    if (!builder) return nullptr;

    // setContentTitle / setContentText
    jmethodID set_title = get_method(env, g_cls.NotificationBuilder,
                                      "setContentTitle",
                                      "(Ljava/lang/CharSequence;)"
                                      "Landroid/app/Notification$Builder;");
    jmethodID set_text = get_method(env, g_cls.NotificationBuilder,
                                     "setContentText",
                                     "(Ljava/lang/CharSequence;)"
                                     "Landroid/app/Notification$Builder;");
    jmethodID set_small_icon = get_method(env, g_cls.NotificationBuilder,
                                           "setSmallIcon",
                                           "(I)"
                                           "Landroid/app/Notification$Builder;");
    jmethodID set_ongoing = get_method(env, g_cls.NotificationBuilder,
                                        "setOngoing",
                                        "(Z)"
                                        "Landroid/app/Notification$Builder;");
    jmethodID set_priority = get_method(env, g_cls.NotificationBuilder,
                                         "setPriority",
                                         "(I)"
                                         "Landroid/app/Notification$Builder;");

    jstring jtitle = std_to_jstring(env, title);
    jstring jtext  = std_to_jstring(env, text);

    env->CallObjectMethod(builder, set_title, jtitle);
    env->CallObjectMethod(builder, set_text, jtext);
    env->CallObjectMethod(builder, set_ongoing, JNI_TRUE);

    // Use a generic notification icon (app's launcher icon)
    // In practice, this would be a resource ID from the app
    jclass r_cls = env->FindClass("android/R$drawable");
    jfieldID ic_stat = get_static_field(env, r_cls, "ic_dialog_info", "I");
    jint icon_id = env->GetStaticIntField(r_cls, ic_stat);
    env->DeleteLocalRef(r_cls);

    env->CallObjectMethod(builder, set_small_icon, icon_id);
    env->CallObjectMethod(builder, set_priority, -1); // PRIORITY_MIN

    env->DeleteLocalRef(jtitle);
    env->DeleteLocalRef(jtext);

    // build()
    jmethodID build = get_method(env, g_cls.NotificationBuilder,
                                  "build", "()Landroid/app/Notification;");
    jobject notification = env->CallObjectMethod(builder, build);
    env->DeleteLocalRef(builder);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }
    return notification;
}

jobject NotificationHelper::build_alert_notification(
    JNIEnv *env, jobject context,
    const std::string &title, const std::string &text) {
    if (!context) return nullptr;

    jmethodID builder_ctor = get_method(env, g_cls.NotificationBuilder,
                                         "<init>",
                                         "(Landroid/content/Context;Ljava/lang/String;)V");
    jstring ch_id = std_to_jstring(env, CHANNEL_ID_ALERTS);
    jobject builder = env->NewObject(g_cls.NotificationBuilder, builder_ctor,
                                      context, ch_id);
    env->DeleteLocalRef(ch_id);
    if (!builder) return nullptr;

    jmethodID set_title = get_method(env, g_cls.NotificationBuilder,
                                      "setContentTitle",
                                      "(Ljava/lang/CharSequence;)"
                                      "Landroid/app/Notification$Builder;");
    jmethodID set_text = get_method(env, g_cls.NotificationBuilder,
                                     "setContentText",
                                     "(Ljava/lang/CharSequence;)"
                                     "Landroid/app/Notification$Builder;");
    jmethodID set_small_icon = get_method(env, g_cls.NotificationBuilder,
                                           "setSmallIcon", "(I)"
                                           "Landroid/app/Notification$Builder;");
    jmethodID set_auto_cancel = get_method(env, g_cls.NotificationBuilder,
                                            "setAutoCancel", "(Z)"
                                            "Landroid/app/Notification$Builder;");

    jstring jtitle = std_to_jstring(env, title);
    jstring jtext  = std_to_jstring(env, text);

    env->CallObjectMethod(builder, set_title, jtitle);
    env->CallObjectMethod(builder, set_text, jtext);
    env->CallObjectMethod(builder, set_auto_cancel, JNI_TRUE);

    jclass r_cls = env->FindClass("android/R$drawable");
    jfieldID ic_stat = get_static_field(env, r_cls, "ic_dialog_alert", "I");
    jint icon_id = env->GetStaticIntField(r_cls, ic_stat);
    env->DeleteLocalRef(r_cls);
    env->CallObjectMethod(builder, set_small_icon, icon_id);

    env->DeleteLocalRef(jtitle);
    env->DeleteLocalRef(jtext);

    jmethodID build = get_method(env, g_cls.NotificationBuilder,
                                  "build", "()Landroid/app/Notification;");
    jobject notification = env->CallObjectMethod(builder, build);
    env->DeleteLocalRef(builder);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return nullptr; }
    return notification;
}

// ===========================================================================
//  SECTION 10 — DISPLAY METRICS & MULTI-DISPLAY
// ===========================================================================

/**
 * Queries display information from the Android DisplayManager.
 *
 * Supports: built-in display, external displays (HDMI, wireless),
 * and virtual displays.
 */
struct DisplayInfo {
    int display_id     = 0;
    int width          = 0;
    int height         = 0;
    float refresh_rate = 60.0f;
    float xdpi         = 320.0f;
    float ydpi         = 320.0f;
    int rotation       = 0;   // Surface.ROTATION_0
    std::string name;
    bool is_presentation = false;  // external / secondary display
};

/** Obtain DisplayInfo for all connected displays. */
inline std::vector<DisplayInfo> get_all_displays(JNIEnv *env,
                                                  jobject context) {
    std::vector<DisplayInfo> displays;

    if (!env || !context) return displays;

    // DisplayManager
    jmethodID get_sys_svc = get_method(env, g_cls.Context,
                                        "getSystemService",
                                        "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring dm_name = std_to_jstring(env, "display");
    jobject dm_local = env->CallObjectMethod(context, get_sys_svc, dm_name);
    env->DeleteLocalRef(dm_name);
    if (!dm_local) return displays;

    jmethodID get_displays = get_method(env, g_cls.DisplayManager,
                                         "getDisplays", "()[Landroid/view/Display;");
    jobjectArray displays_arr = static_cast<jobjectArray>(
        env->CallObjectMethod(dm_local, get_displays));
    if (!displays_arr) { env->DeleteLocalRef(dm_local); return displays; }

    jsize count = env->GetArrayLength(displays_arr);

    // Prepare method IDs for Display methods
    jmethodID get_display_id = get_method(env, g_cls.Display,
                                           "getDisplayId", "()I");
    // getRealSize(Point)
    jmethodID get_real_size = get_method(env, g_cls.Display,
                                          "getRealSize",
                                          "(Landroid/graphics/Point;)V");
    jmethodID get_rotation = get_method(env, g_cls.Display,
                                         "getRotation", "()I");
    jmethodID get_name = get_method(env, g_cls.Display,
                                     "getName", "()Ljava/lang/String;");
    // getRefreshRate() — available API 21+
    jmethodID get_refresh = nullptr;
    if (android_sdk_version() >= 21) {
        get_refresh = get_method(env, g_cls.Display,
                                  "getRefreshRate", "()F");
    }

    jmethodID point_ctor = get_method(env, g_cls.Point, "<init>", "(II)V");
    jobject point = env->NewObject(g_cls.Point, point_ctor, 0, 0);

    for (jsize i = 0; i < count; ++i) {
        jobject display = env->GetObjectArrayElement(displays_arr, i);
        if (!display) continue;

        DisplayInfo info;
        info.display_id = env->CallIntMethod(display, get_display_id);

        env->CallVoidMethod(display, get_real_size, point);
        jfieldID x_fid = get_field(env, g_cls.Point, "x", "I");
        jfieldID y_fid = get_field(env, g_cls.Point, "y", "I");
        info.width  = env->GetIntField(point, x_fid);
        info.height = env->GetIntField(point, y_fid);

        info.rotation = env->CallIntMethod(display, get_rotation);

        if (get_refresh) {
            info.refresh_rate = env->CallFloatMethod(display, get_refresh);
        }

        jstring name_j = static_cast<jstring>(
            env->CallObjectMethod(display, get_name));
        info.name = jstring_to_std(env, name_j);
        if (name_j) env->DeleteLocalRef(name_j);

        // Determine if this is a presentation display
        jmethodID get_flags = nullptr;
        if (android_sdk_version() >= 17) {
            get_flags = get_method(env, g_cls.Display, "getFlags", "()I");
        }
        if (get_flags) {
            jint flags = env->CallIntMethod(display, get_flags);
            constexpr jint FLAG_PRESENTATION = 8;
            info.is_presentation = (flags & FLAG_PRESENTATION) != 0;
        }

        displays.push_back(std::move(info));
        env->DeleteLocalRef(display);
    }

    env->DeleteLocalRef(point);
    env->DeleteLocalRef(displays_arr);
    env->DeleteLocalRef(dm_local);

    return displays;
}

/**
 * Convenience: get the primary (built-in) display info.
 */
inline std::optional<DisplayInfo> get_primary_display(JNIEnv *env,
                                                       jobject context) {
    auto displays = get_all_displays(env, context);
    for (auto &d : displays) {
        if (d.display_id == 0) return d;
    }
    return displays.empty() ? std::nullopt
                            : std::optional<DisplayInfo>(displays[0]);
}

// ===========================================================================
//  SECTION 11 — BATTERY OPTIMISATION EXEMPTION
// ===========================================================================

/**
 * Requests exemption from Android's battery optimisation (Doze).
 *
 * Without this, the system may throttle or kill the background service
 * when the device is idle.
 */
struct BatteryHelper {
    /**
     * Check if the app is already exempt from battery optimisation.
     */
    static bool is_exempt(JNIEnv *env, jobject context) {
        if (!env || !context) return false;

        jmethodID get_sys_svc = get_method(env, g_cls.Context,
                                            "getSystemService",
                                            "(Ljava/lang/String;)Ljava/lang/Object;");
        jstring pw_name = std_to_jstring(env, "power");
        jobject pm = env->CallObjectMethod(context, get_sys_svc, pw_name);
        env->DeleteLocalRef(pw_name);
        if (!pm) return false;

        jmethodID is_ignoring = get_method(env, g_cls.PowerManager,
                                            "isIgnoringBatteryOptimizations",
                                            "(Ljava/lang/String;)Z");
        if (!is_ignoring) { env->DeleteLocalRef(pm); return false; }

        // Get package name from context
        jmethodID get_pkg = get_method(env, g_cls.Context,
                                        "getPackageName",
                                        "()Ljava/lang/String;");
        jstring pkg = static_cast<jstring>(
            env->CallObjectMethod(context, get_pkg));
        jboolean ignoring = env->CallBooleanMethod(pm, is_ignoring, pkg);
        env->DeleteLocalRef(pkg);
        env->DeleteLocalRef(pm);
        return ignoring == JNI_TRUE;
    }

    /**
     * Open the system settings page to request ignoring battery
     * optimisation.  The user must manually approve.
     */
    static bool request_exemption(JNIEnv *env, jobject context) {
        if (!env || !context) return false;
        if (is_exempt(env, context)) return true;

        // ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS
        jstring action = std_to_jstring(
            env, "android.settings.REQUEST_IGNORE_BATTERY_OPTIMIZATIONS");
        jmethodID intent_ctor = get_method(env, g_cls.Intent, "<init>",
                                            "(Ljava/lang/String;)V");
        jobject intent = env->NewObject(g_cls.Intent, intent_ctor, action);
        env->DeleteLocalRef(action);

        // Set data URI to package-specific
        jmethodID get_pkg = get_method(env, g_cls.Context,
                                        "getPackageName",
                                        "()Ljava/lang/String;");
        jstring pkg = static_cast<jstring>(
            env->CallObjectMethod(context, get_pkg));
        jstring uri_str = std_to_jstring(env, "package:" +
                                         jstring_to_std(env, pkg));
        jmethodID parse_uri = get_static_method(env, g_cls.Uri, "parse",
                                                 "(Ljava/lang/String;)Landroid/net/Uri;");
        jobject uri = env->CallStaticObjectMethod(g_cls.Uri, parse_uri,
                                                   uri_str);
        jmethodID set_data = get_method(env, g_cls.Intent, "setData",
                                         "(Landroid/net/Uri;)Landroid/content/Intent;");
        env->CallObjectMethod(intent, set_data, uri);

        // Add FLAG_ACTIVITY_NEW_TASK
        jmethodID add_flags = get_method(env, g_cls.Intent, "addFlags",
                                          "(I)Landroid/content/Intent;");
        constexpr jint FLAG_ACTIVITY_NEW_TASK = 0x10000000;
        env->CallObjectMethod(intent, add_flags, FLAG_ACTIVITY_NEW_TASK);

        jmethodID start_activity = get_method(env, g_cls.Context,
                                               "startActivity",
                                               "(Landroid/content/Intent;)V");
        env->CallVoidMethod(context, start_activity, intent);

        env->DeleteLocalRef(intent);
        env->DeleteLocalRef(uri);
        env->DeleteLocalRef(uri_str);
        env->DeleteLocalRef(pkg);

        spdlog::info("[android] Battery optimisation exemption requested");
        return !(env->ExceptionCheck());
    }

    /**
     * Determine battery level as a percentage (0–100).
     */
    static int get_battery_level(JNIEnv *env, jobject context) {
        if (!env || !context) return -1;

        // Register a null BroadcastReceiver for ACTION_BATTERY_CHANGED
        jstring action = std_to_jstring(env, "android.intent.action.BATTERY_CHANGED");
        jmethodID intent_ctor = get_method(env, g_cls.Intent, "<init>",
                                            "(Ljava/lang/String;)V");
        jobject filter = env->NewObject(g_cls.Intent, intent_ctor, action);
        env->DeleteLocalRef(action);

        jmethodID register = get_method(env, g_cls.Context,
                                         "registerReceiver",
                                         "(Landroid/content/BroadcastReceiver;"
                                         "Landroid/content/IntentFilter;)"
                                         "Landroid/content/Intent;");
        jobject battery_intent = env->CallObjectMethod(
            context, register, nullptr, filter);
        env->DeleteLocalRef(filter);

        if (!battery_intent) return -1;

        jclass intent_cls = env->GetObjectClass(battery_intent);
        jmethodID get_int_extra = get_method(env, intent_cls, "getIntExtra",
                                              "(Ljava/lang/String;I)I");
        jstring level_key = std_to_jstring(env, "level");
        jstring scale_key = std_to_jstring(env, "scale");

        jint level = env->CallIntMethod(battery_intent, get_int_extra,
                                         level_key, -1);
        jint scale = env->CallIntMethod(battery_intent, get_int_extra,
                                         scale_key, 100);

        env->DeleteLocalRef(level_key);
        env->DeleteLocalRef(scale_key);
        env->DeleteLocalRef(intent_cls);
        env->DeleteLocalRef(battery_intent);

        if (level < 0 || scale <= 0) return -1;
        return static_cast<int>(level * 100 / scale);
    }

    /**
     * Check if the device is currently charging.
     */
    static bool is_charging(JNIEnv *env, jobject context) {
        if (!env || !context) return false;

        jstring action = std_to_jstring(env,
                                         "android.intent.action.BATTERY_CHANGED");
        jmethodID intent_ctor = get_method(env, g_cls.Intent, "<init>",
                                            "(Ljava/lang/String;)V");
        jobject filter = env->NewObject(g_cls.Intent, intent_ctor, action);
        env->DeleteLocalRef(action);

        jmethodID register = get_method(env, g_cls.Context,
                                         "registerReceiver",
                                         "(Landroid/content/BroadcastReceiver;"
                                         "Landroid/content/IntentFilter;)"
                                         "Landroid/content/Intent;");
        jobject intent = env->CallObjectMethod(context, register,
                                                nullptr, filter);
        env->DeleteLocalRef(filter);
        if (!intent) return false;

        jclass intent_cls = env->GetObjectClass(intent);
        jmethodID get_int_extra = get_method(env, intent_cls, "getIntExtra",
                                              "(Ljava/lang/String;I)I");
        jstring status_key = std_to_jstring(env, "status");

        jint status = env->CallIntMethod(intent, get_int_extra,
                                          status_key, 1); // BATTERY_STATUS_UNKNOWN
        env->DeleteLocalRef(status_key);
        env->DeleteLocalRef(intent_cls);
        env->DeleteLocalRef(intent);

        // BATTERY_STATUS_CHARGING = 2
        // BATTERY_STATUS_FULL     = 5
        return status == 2 || status == 5;
    }
};

// ===========================================================================
//  SECTION 12 — STORAGE ACCESS FRAMEWORK (SAF)
// ===========================================================================

/**
 * Provides file access via Android's Storage Access Framework.
 *
 * SAF is required on Android 10+ (API 29+) for accessing files outside
 * the app's scoped storage.  We support:
 *   - Opening a document tree (persistent URI permission)
 *   - Reading / writing files via DocumentFile
 *   - Traditional java.io.File for app-private storage
 */
struct SafHelper {
    jobject tree_uri      = nullptr;  // persisted document tree URI
    jobject document_tree = nullptr;  // DocumentFile representing the tree

    /**
     * Request the user to pick a directory via ACTION_OPEN_DOCUMENT_TREE.
     * The resulting URI should be persisted with takePersistableUriPermission.
     */
    static bool request_directory_access(JNIEnv *env, jobject activity,
                                          int request_code) {
        if (!env || !activity) return false;

        jstring action = std_to_jstring(env,
                                         "android.intent.action.OPEN_DOCUMENT_TREE");
        jmethodID intent_ctor = get_method(env, g_cls.Intent, "<init>",
                                            "(Ljava/lang/String;)V");
        jobject intent = env->NewObject(g_cls.Intent, intent_ctor, action);
        env->DeleteLocalRef(action);

        if (!intent) return false;

        jmethodID start_for_result = get_method(env, g_cls.Activity,
                                                 "startActivityForResult",
                                                 "(Landroid/content/Intent;I)V");
        env->CallVoidMethod(activity, start_for_result, intent, request_code);
        env->DeleteLocalRef(intent);
        if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
        return true;
    }

    /**
     * Persist the URI permission obtained from a document tree picker.
     */
    bool persist_uri_permission(JNIEnv *env, jobject context,
                                 jobject uri) {
        if (!env || !context || !uri) return false;

        // Take read + write permission
        jmethodID take_perm = get_method(env, g_cls.ContentResolver,
                                          "takePersistableUriPermission",
                                          "(Landroid/net/Uri;I)V");
        jmethodID get_resolver = get_method(env, g_cls.Context,
                                             "getContentResolver",
                                             "()Landroid/content/ContentResolver;");
        jobject resolver = env->CallObjectMethod(context, get_resolver);
        if (!resolver) return false;

        constexpr jint FLAG_GRANT_READ_URI_PERMISSION  = 0x1;
        constexpr jint FLAG_GRANT_WRITE_URI_PERMISSION = 0x2;

        env->CallVoidMethod(resolver, take_perm, uri,
                             FLAG_GRANT_READ_URI_PERMISSION |
                             FLAG_GRANT_WRITE_URI_PERMISSION);
        env->DeleteLocalRef(resolver);

        if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }

        // Store the URI
        if (tree_uri) env->DeleteGlobalRef(tree_uri);
        tree_uri = env->NewGlobalRef(uri);

        spdlog::info("[android] SAF URI permission persisted");
        return true;
    }

    /**
     * List files in a DocumentFile directory.
     */
    std::vector<std::string> list_files(JNIEnv *env, jobject directory) {
        std::vector<std::string> files;
        if (!env || !directory) return files;

        jmethodID list = get_method(env, g_cls.DocumentFile,
                                     "listFiles",
                                     "()[Landroidx/documentfile/provider/DocumentFile;");
        jobjectArray arr = static_cast<jobjectArray>(
            env->CallObjectMethod(directory, list));
        if (!arr) return files;

        jsize count = env->GetArrayLength(arr);
        jmethodID get_name = get_method(env, g_cls.DocumentFile,
                                         "getName", "()Ljava/lang/String;");

        for (jsize i = 0; i < count; ++i) {
            jobject file = env->GetObjectArrayElement(arr, i);
            jstring name_j = static_cast<jstring>(
                env->CallObjectMethod(file, get_name));
            files.push_back(jstring_to_std(env, name_j));
            env->DeleteLocalRef(name_j);
            env->DeleteLocalRef(file);
        }
        env->DeleteLocalRef(arr);
        return files;
    }

    /**
     * Read a file via SAF into a byte vector.
     */
    std::vector<uint8_t> read_file(JNIEnv *env, jobject context,
                                    jobject document_file) {
        std::vector<uint8_t> data;
        if (!env || !context || !document_file) return data;

        // Get URI from DocumentFile
        jmethodID get_uri = get_method(env, g_cls.DocumentFile,
                                        "getUri", "()Landroid/net/Uri;");
        jobject uri = env->CallObjectMethod(document_file, get_uri);
        if (!uri) return data;

        // Open InputStream
        jmethodID get_resolver = get_method(env, g_cls.Context,
                                             "getContentResolver",
                                             "()Landroid/content/ContentResolver;");
        jobject resolver = env->CallObjectMethod(context, get_resolver);

        jmethodID open_is = get_method(env, g_cls.ContentResolver,
                                        "openInputStream",
                                        "(Landroid/net/Uri;)Ljava/io/InputStream;");
        jobject input_stream = env->CallObjectMethod(resolver, open_is, uri);

        if (input_stream) {
            jclass is_cls = env->GetObjectClass(input_stream);
            jmethodID read = get_method(env, is_cls, "read", "([B)I");
            jmethodID close = get_method(env, is_cls, "close", "()V");

            constexpr jsize CHUNK = 4096;
            jbyteArray buf = env->NewByteArray(CHUNK);

            while (true) {
                jint n = env->CallIntMethod(input_stream, read, buf);
                if (n <= 0) break;
                jsize old_sz = static_cast<jsize>(data.size());
                data.resize(old_sz + static_cast<size_t>(n));
                env->GetByteArrayRegion(buf, 0, n,
                                        reinterpret_cast<jbyte *>(data.data() + old_sz));
            }

            env->DeleteLocalRef(buf);
            env->CallVoidMethod(input_stream, close);
            env->DeleteLocalRef(is_cls);
            env->DeleteLocalRef(input_stream);
        }

        env->DeleteLocalRef(resolver);
        env->DeleteLocalRef(uri);
        return data;
    }

    /**
     * Write data to a file via SAF.
     */
    bool write_file(JNIEnv *env, jobject context, jobject document_file,
                    const uint8_t *data, size_t len) {
        if (!env || !context || !document_file || !data) return false;

        jmethodID get_uri = get_method(env, g_cls.DocumentFile,
                                        "getUri", "()Landroid/net/Uri;");
        jobject uri = env->CallObjectMethod(document_file, get_uri);

        jmethodID get_resolver = get_method(env, g_cls.Context,
                                             "getContentResolver",
                                             "()Landroid/content/ContentResolver;");
        jobject resolver = env->CallObjectMethod(context, get_resolver);

        jmethodID open_os = get_method(env, g_cls.ContentResolver,
                                        "openOutputStream",
                                        "(Landroid/net/Uri;)Ljava/io/OutputStream;");
        jobject output_stream = env->CallObjectMethod(resolver, open_os, uri);

        if (output_stream) {
            jclass os_cls = env->GetObjectClass(output_stream);
            jmethodID write = get_method(env, os_cls, "write", "([B)V");
            jmethodID close = get_method(env, os_cls, "close", "()V");

            jbyteArray buf = env->NewByteArray(static_cast<jsize>(len));
            env->SetByteArrayRegion(buf, 0, static_cast<jsize>(len),
                                     reinterpret_cast<const jbyte *>(data));
            env->CallVoidMethod(output_stream, write, buf);
            env->DeleteLocalRef(buf);
            env->CallVoidMethod(output_stream, close);
            env->DeleteLocalRef(os_cls);
            env->DeleteLocalRef(output_stream);
        }

        env->DeleteLocalRef(resolver);
        env->DeleteLocalRef(uri);
        if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
        return true;
    }
};

// ===========================================================================
//  SECTION 13 — PERMISSION HANDLING
// ===========================================================================

/**
 * Checks and requests Android runtime permissions.
 *
 * Required permissions for cppdesk server:
 *   - RECORD_AUDIO               (microphone)
 *   - FOREGROUND_SERVICE          (API 28+)
 *   - SYSTEM_ALERT_WINDOW         (overlay / floating window)
 *   - POST_NOTIFICATIONS          (API 33+)
 *   - FOREGROUND_SERVICE_MEDIA_PROJECTION  (API 34+)
 *   - FOREGROUND_SERVICE_MICROPHONE
 */
struct PermissionHelper {
    // Permission string constants
    static constexpr const char *RECORD_AUDIO  = "android.permission.RECORD_AUDIO";
    static constexpr const char *FOREGROUND_SERVICE =
        "android.permission.FOREGROUND_SERVICE";
    static constexpr const char *SYSTEM_ALERT_WINDOW =
        "android.permission.SYSTEM_ALERT_WINDOW";
    static constexpr const char *POST_NOTIFICATIONS =
        "android.permission.POST_NOTIFICATIONS";
    static constexpr const char *FOREGROUND_SERVICE_MEDIA_PROJECTION =
        "android.permission.FOREGROUND_SERVICE_MEDIA_PROJECTION";
    static constexpr const char *FOREGROUND_SERVICE_MICROPHONE =
        "android.permission.FOREGROUND_SERVICE_MICROPHONE";
    static constexpr const char *FOREGROUND_SERVICE_CONNECTED_DEVICE =
        "android.permission.FOREGROUND_SERVICE_CONNECTED_DEVICE";

    /**
     * Check if a specific permission is granted.
     */
    static bool is_granted(JNIEnv *env, jobject context,
                            const char *permission) {
        if (!env || !context) return false;
        jmethodID check = get_method(env, g_cls.Context,
                                      "checkSelfPermission",
                                      "(Ljava/lang/String;)I");
        jstring perm = std_to_jstring(env, permission);
        jint result = env->CallIntMethod(context, check, perm);
        env->DeleteLocalRef(perm);
        // PERMISSION_GRANTED = 0
        return result == 0;
    }

    /**
     * Check all critical permissions and return a list of missing ones.
     */
    static std::vector<std::string> missing_permissions(JNIEnv *env,
                                                         jobject context) {
        std::vector<std::string> missing;
        int sdk = android_sdk_version();

        // Always needed
        if (!is_granted(env, context, RECORD_AUDIO))
            missing.emplace_back(RECORD_AUDIO);

        if (sdk >= 28 && !is_granted(env, context, FOREGROUND_SERVICE))
            missing.emplace_back(FOREGROUND_SERVICE);

        if (sdk >= 33 && !is_granted(env, context, POST_NOTIFICATIONS))
            missing.emplace_back(POST_NOTIFICATIONS);

        if (sdk >= 34) {
            if (!is_granted(env, context, FOREGROUND_SERVICE_MEDIA_PROJECTION))
                missing.emplace_back(FOREGROUND_SERVICE_MEDIA_PROJECTION);
            if (!is_granted(env, context, FOREGROUND_SERVICE_MICROPHONE))
                missing.emplace_back(FOREGROUND_SERVICE_MICROPHONE);
            if (!is_granted(env, context, FOREGROUND_SERVICE_CONNECTED_DEVICE))
                missing.emplace_back(FOREGROUND_SERVICE_CONNECTED_DEVICE);
        }

        return missing;
    }

    /**
     * Request permissions from an Activity.
     */
    static bool request_permissions(JNIEnv *env, jobject activity,
                                     const std::vector<std::string> &permissions,
                                     int request_code) {
        if (!env || !activity || permissions.empty()) return true;

        jclass string_cls = env->FindClass("java/lang/String");
        jobjectArray perms_arr = env->NewObjectArray(
            static_cast<jsize>(permissions.size()), string_cls, nullptr);
        env->DeleteLocalRef(string_cls);

        for (size_t i = 0; i < permissions.size(); ++i) {
            jstring p = std_to_jstring(env, permissions[i]);
            env->SetObjectArrayElement(perms_arr, static_cast<jsize>(i), p);
            env->DeleteLocalRef(p);
        }

        jmethodID request = get_method(env, g_cls.Activity,
                                        "requestPermissions",
                                        "([Ljava/lang/String;I)V");
        env->CallVoidMethod(activity, request, perms_arr, request_code);
        env->DeleteLocalRef(perms_arr);

        if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
        spdlog::info("[android] Requesting {} permissions", permissions.size());
        return true;
    }

    /**
     * Check if SYSTEM_ALERT_WINDOW is granted (special, not in manifest).
     */
    static bool can_draw_overlays(JNIEnv *env, jobject context) {
        if (android_sdk_version() < 23) return true; // pre-Marshmallow
        jstring action = std_to_jstring(env,
                                         "android.settings.action.MANAGE_OVERLAY_PERMISSION");
        jmethodID intent_ctor = get_method(env, g_cls.Intent, "<init>",
                                            "(Ljava/lang/String;)V");
        // Settings.canDrawOverlays(context)
        jclass settings_cls = cache_class(env, "android/provider/Settings");
        jmethodID can_draw = get_static_method(env, settings_cls,
                                                "canDrawOverlays",
                                                "(Landroid/content/Context;)Z");
        if (!can_draw) { env->DeleteLocalRef(action); env->DeleteLocalRef(settings_cls); return false; }
        jboolean ok = env->CallStaticBooleanMethod(settings_cls, can_draw,
                                                    context);
        env->DeleteLocalRef(action);
        env->DeleteLocalRef(settings_cls);
        return ok == JNI_TRUE;
    }
};

// ===========================================================================
//  SECTION 14 — MISCELLANEOUS HELPERS
// ===========================================================================

// ---------------------------------------------------------------------------
//  14.1  Clipboard access
// ---------------------------------------------------------------------------
/**
 * Read the current clipboard text via ClipboardManager.
 */
inline std::string get_clipboard_text(JNIEnv *env, jobject context) {
    if (!env || !context) return {};

    jmethodID get_sys_svc = get_method(env, g_cls.Context,
                                        "getSystemService",
                                        "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring clpb = std_to_jstring(env, "clipboard");
    jobject cm = env->CallObjectMethod(context, get_sys_svc, clpb);
    env->DeleteLocalRef(clpb);
    if (!cm) return {};

    jclass cm_cls = env->GetObjectClass(cm);
    jmethodID get_primary = get_method(env, cm_cls, "getPrimaryClip",
                                        "()Landroid/content/ClipData;");
    jobject clip = env->CallObjectMethod(cm, get_primary);
    if (!clip) { env->DeleteLocalRef(cm); env->DeleteLocalRef(cm_cls); return {}; }

    jclass cd_cls = env->GetObjectClass(clip);
    jmethodID get_item_at = get_method(env, cd_cls, "getItemAt",
                                        "(I)Landroid/content/ClipData$Item;");
    jobject item = env->CallObjectMethod(clip, get_item_at, 0);
    if (!item) {
        env->DeleteLocalRef(cd_cls);
        env->DeleteLocalRef(clip);
        env->DeleteLocalRef(cm_cls);
        env->DeleteLocalRef(cm);
        return {};
    }

    jclass item_cls = env->GetObjectClass(item);
    jmethodID get_text = get_method(env, item_cls, "getText",
                                     "()Ljava/lang/CharSequence;");
    jobject cs = env->CallObjectMethod(item, get_text);
    std::string result;
    if (cs) {
        jstring js = static_cast<jstring>(env->CallObjectMethod(
            cs, get_method(env, env->GetObjectClass(cs),
                           "toString", "()Ljava/lang/String;")));
        result = jstring_to_std(env, js);
        env->DeleteLocalRef(js);
        env->DeleteLocalRef(cs);
    }

    env->DeleteLocalRef(item_cls);
    env->DeleteLocalRef(item);
    env->DeleteLocalRef(cd_cls);
    env->DeleteLocalRef(clip);
    env->DeleteLocalRef(cm_cls);
    env->DeleteLocalRef(cm);
    return result;
}

/**
 * Set clipboard text.
 */
inline bool set_clipboard_text(JNIEnv *env, jobject context,
                                const std::string &text) {
    if (!env || !context) return false;

    jmethodID get_sys_svc = get_method(env, g_cls.Context,
                                        "getSystemService",
                                        "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring clpb = std_to_jstring(env, "clipboard");
    jobject cm = env->CallObjectMethod(context, get_sys_svc, clpb);
    env->DeleteLocalRef(clpb);
    if (!cm) return false;

    jclass cm_cls = env->GetObjectClass(cm);
    // ClipData.newPlainText(label, text)
    jclass cd_cls = env->FindClass("android/content/ClipData");
    jmethodID new_plain = get_static_method(env, cd_cls, "newPlainText",
                                             "(Ljava/lang/CharSequence;"
                                             "Ljava/lang/CharSequence;)"
                                             "Landroid/content/ClipData;");
    jstring label = std_to_jstring(env, "cppdesk");
    jstring jtext = std_to_jstring(env, text);
    jobject clip = env->CallStaticObjectMethod(cd_cls, new_plain,
                                                label, jtext);
    env->DeleteLocalRef(label);
    env->DeleteLocalRef(jtext);

    jmethodID set_primary = get_method(env, cm_cls, "setPrimaryClip",
                                        "(Landroid/content/ClipData;)V");
    env->CallVoidMethod(cm, set_primary, clip);

    env->DeleteLocalRef(clip);
    env->DeleteLocalRef(cd_cls);
    env->DeleteLocalRef(cm_cls);
    env->DeleteLocalRef(cm);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
    return true;
}

// ---------------------------------------------------------------------------
//  14.2  System bar visibility (immersive mode)
// ---------------------------------------------------------------------------
inline void set_system_bars_visible(JNIEnv *env, jobject activity,
                                     bool visible) {
    if (!env || !activity) return;

    jmethodID get_window = get_method(env, g_cls.Activity,
                                       "getWindow", "()Landroid/view/Window;");
    jobject window = env->CallObjectMethod(activity, get_window);
    if (!window) return;

    jclass win_cls = env->GetObjectClass(window);
    jmethodID get_decor = get_method(env, win_cls, "getDecorView",
                                      "()Landroid/view/View;");
    jobject decor = env->CallObjectMethod(window, get_decor);

    if (decor) {
        jclass view_cls = env->GetObjectClass(decor);
        jmethodID set_sys_ui = get_method(env, view_cls,
                                           "setSystemUiVisibility", "(I)V");
        jint flags = visible ? 0 : (0x2 | 0x4 | 0x100 | 0x200 | 0x400 | 0x800);
        // SYSTEM_UI_FLAG_FULLSCREEN          = 0x4
        // SYSTEM_UI_FLAG_HIDE_NAVIGATION    = 0x2
        // SYSTEM_UI_FLAG_IMMERSIVE_STICKY   = 0x1000
        // SYSTEM_UI_FLAG_LAYOUT_STABLE      = 0x100
        // SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION = 0x200
        // SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN  = 0x400
        env->CallVoidMethod(decor, set_sys_ui, flags);
        env->DeleteLocalRef(view_cls);
        env->DeleteLocalRef(decor);
    }

    env->DeleteLocalRef(win_cls);
    env->DeleteLocalRef(window);
}

// ---------------------------------------------------------------------------
//  14.3  Device info
// ---------------------------------------------------------------------------
struct DeviceInfo {
    std::string manufacturer;
    std::string model;
    std::string product;
    std::string board;
    std::string os_version;
    int sdk_version = 0;
};

inline DeviceInfo get_device_info(JNIEnv *env) {
    DeviceInfo info;
    info.sdk_version = android_sdk_version();

    jfieldID fid;

    fid = get_static_field(env, g_cls.Build_VERSION, "RELEASE",
                            "Ljava/lang/String;");
    if (fid) {
        jstring js = static_cast<jstring>(
            env->GetStaticObjectField(g_cls.Build_VERSION, fid));
        info.os_version = jstring_to_std(env, js);
        if (js) env->DeleteLocalRef(js);
    }

    // android.os.Build fields
    jclass build_cls = env->FindClass("android/os/Build");
    if (!build_cls) return info;

    fid = get_static_field(env, build_cls, "MANUFACTURER",
                            "Ljava/lang/String;");
    if (fid) {
        jstring js = static_cast<jstring>(env->GetStaticObjectField(build_cls, fid));
        info.manufacturer = jstring_to_std(env, js);
        if (js) env->DeleteLocalRef(js);
    }

    fid = get_static_field(env, build_cls, "MODEL", "Ljava/lang/String;");
    if (fid) {
        jstring js = static_cast<jstring>(env->GetStaticObjectField(build_cls, fid));
        info.model = jstring_to_std(env, js);
        if (js) env->DeleteLocalRef(js);
    }

    fid = get_static_field(env, build_cls, "PRODUCT", "Ljava/lang/String;");
    if (fid) {
        jstring js = static_cast<jstring>(env->GetStaticObjectField(build_cls, fid));
        info.product = jstring_to_std(env, js);
        if (js) env->DeleteLocalRef(js);
    }

    fid = get_static_field(env, build_cls, "BOARD", "Ljava/lang/String;");
    if (fid) {
        jstring js = static_cast<jstring>(env->GetStaticObjectField(build_cls, fid));
        info.board = jstring_to_std(env, js);
        if (js) env->DeleteLocalRef(js);
    }

    env->DeleteLocalRef(build_cls);
    return info;
}

// ---------------------------------------------------------------------------
//  14.4  Keep screen on (via WindowManager.LayoutParams)
// ---------------------------------------------------------------------------
inline void set_keep_screen_on(JNIEnv *env, jobject activity, bool on) {
    if (!env || !activity) return;

    jmethodID get_window = get_method(env, g_cls.Activity,
                                       "getWindow", "()Landroid/view/Window;");
    jobject window = env->CallObjectMethod(activity, get_window);

    jclass wm_lp_cls = env->FindClass("android/view/WindowManager$LayoutParams");
    jfieldID fl_kso = get_static_field(env, wm_lp_cls,
                                        "FLAG_KEEP_SCREEN_ON", "I");
    jint flag = env->GetStaticIntField(wm_lp_cls, fl_kso);
    env->DeleteLocalRef(wm_lp_cls);

    jclass win_cls = env->GetObjectClass(window);
    jmethodID add_flags = get_method(env, win_cls, "addFlags", "(I)V");
    jmethodID clear_flags = get_method(env, win_cls, "clearFlags", "(I)V");

    if (on) {
        env->CallVoidMethod(window, add_flags, flag);
    } else {
        env->CallVoidMethod(window, clear_flags, flag);
    }

    env->DeleteLocalRef(win_cls);
    env->DeleteLocalRef(window);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

// ---------------------------------------------------------------------------
//  14.5  Screen orientation lock
// ---------------------------------------------------------------------------
inline void set_orientation_lock(JNIEnv *env, jobject activity,
                                  int orientation /* 0=auto, 1=portrait, 2=landscape */) {
    if (!env || !activity) return;
    jmethodID set_ro = get_method(env, g_cls.Activity,
                                   "setRequestedOrientation", "(I)V");
    int val = orientation;
    switch (orientation) {
    case 1:  val = 1;  break; // SCREEN_ORIENTATION_PORTRAIT
    case 2:  val = 0;  break; // SCREEN_ORIENTATION_LANDSCAPE
    default: val = -1; break; // SCREEN_ORIENTATION_UNSPECIFIED
    }
    env->CallVoidMethod(activity, set_ro, val);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

// ---------------------------------------------------------------------------
//  14.6  AudioManager utilities
// ---------------------------------------------------------------------------
enum class AudioMode {
    Normal       = 0,
    Ringtone     = 1,
    InCall       = 2,
    InCommunication = 3,
};

inline void set_audio_mode(JNIEnv *env, jobject context, AudioMode mode) {
    if (!env || !context) return;

    jmethodID get_sys_svc = get_method(env, g_cls.Context,
                                        "getSystemService",
                                        "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring audio = std_to_jstring(env, "audio");
    jobject am = env->CallObjectMethod(context, get_sys_svc, audio);
    env->DeleteLocalRef(audio);
    if (!am) return;

    jmethodID set_mode = get_method(env, g_cls.AudioManager,
                                     "setMode", "(I)V");
    env->CallVoidMethod(am, set_mode, static_cast<jint>(mode));
    env->DeleteLocalRef(am);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

// ---------------------------------------------------------------------------
//  14.7  Vibration / haptic feedback
// ---------------------------------------------------------------------------
inline void vibrate(JNIEnv *env, jobject context, int64_t duration_ms) {
    if (!env || !context) return;

    jmethodID get_sys_svc = get_method(env, g_cls.Context,
                                        "getSystemService",
                                        "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring vibrator = std_to_jstring(env, "vibrator");
    jobject v = env->CallObjectMethod(context, get_sys_svc, vibrator);
    env->DeleteLocalRef(vibrator);
    if (!v) return;

    jclass v_cls = env->GetObjectClass(v);
    if (android_sdk_version() >= 26) {
        jmethodID vibrate_mid = get_method(env, v_cls, "vibrate",
                                            "(Landroid/os/VibrationEffect;)V");
        jclass ve_cls = env->FindClass("android/os/VibrationEffect");
        jmethodID create_one_shot = get_static_method(
            env, ve_cls, "createOneShot",
            "(JI)Landroid/os/VibrationEffect;");
        jobject effect = env->CallStaticObjectMethod(
            ve_cls, create_one_shot, duration_ms, -1); // DEFAULT_AMPLITUDE
        env->CallVoidMethod(v, vibrate_mid, effect);
        env->DeleteLocalRef(effect);
        env->DeleteLocalRef(ve_cls);
    } else {
        jmethodID vibrate_mid = get_method(env, v_cls, "vibrate", "(J)V");
        env->CallVoidMethod(v, vibrate_mid, duration_ms);
    }

    env->DeleteLocalRef(v_cls);
    env->DeleteLocalRef(v);
    if (env->ExceptionCheck()) env->ExceptionClear();
}

// ---------------------------------------------------------------------------
//  14.8  Network connectivity check
// ---------------------------------------------------------------------------
inline bool is_network_connected(JNIEnv *env, jobject context) {
    if (!env || !context) return false;

    jmethodID get_sys_svc = get_method(env, g_cls.Context,
                                        "getSystemService",
                                        "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring conn = std_to_jstring(env, "connectivity");
    jobject cm = env->CallObjectMethod(context, get_sys_svc, conn);
    env->DeleteLocalRef(conn);
    if (!cm) return false;

    jclass cm_cls = env->GetObjectClass(cm);
    jmethodID get_active = get_method(env, cm_cls, "getActiveNetworkInfo",
                                       "()Landroid/net/NetworkInfo;");
    jobject net_info = env->CallObjectMethod(cm, get_active);
    if (!net_info) { env->DeleteLocalRef(cm_cls); env->DeleteLocalRef(cm); return false; }

    jclass ni_cls = env->GetObjectClass(net_info);
    jmethodID is_connected = get_method(env, ni_cls,
                                         "isConnected", "()Z");
    jboolean connected = env->CallBooleanMethod(net_info, is_connected);

    env->DeleteLocalRef(ni_cls);
    env->DeleteLocalRef(net_info);
    env->DeleteLocalRef(cm_cls);
    env->DeleteLocalRef(cm);
    return connected == JNI_TRUE;
}

// ---------------------------------------------------------------------------
//  14.9  Thermal status
// ---------------------------------------------------------------------------
inline int get_thermal_status(JNIEnv *env, jobject context) {
    if (!env || !context) return 0; // STATUS_NONE

    jmethodID get_sys_svc = get_method(env, g_cls.Context,
                                        "getSystemService",
                                        "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring pw_name = std_to_jstring(env, "power");
    jobject pm = env->CallObjectMethod(context, get_sys_svc, pw_name);
    env->DeleteLocalRef(pw_name);
    if (!pm) return 0;

    jmethodID get_thermal = nullptr;
    if (android_sdk_version() >= 29) {
        get_thermal = get_method(env, g_cls.PowerManager,
                                  "getCurrentThermalStatus", "()I");
    }
    if (!get_thermal) { env->DeleteLocalRef(pm); return 0; }

    jint status = env->CallIntMethod(pm, get_thermal);
    env->DeleteLocalRef(pm);
    // 0=STATUS_NONE, 1=STATUS_LIGHT, 2=STATUS_MODERATE,
    // 3=STATUS_SEVERE, 4=STATUS_CRITICAL, 5=STATUS_EMERGENCY, 6=STATUS_SHUTDOWN
    return status;
}

// ===========================================================================
//  SECTION 15 — PLATFORM LIFECYCLE
// ===========================================================================

/**
 * Singleton state for the Android platform backend.
 */
struct PlatformState {
    MediaProjectionCapture  capture;
    MediaCodecEncoder       encoder;
    AudioCapturer           microphone;
    AudioPlayer             speaker;
    InputInjector           input;
    ForegroundService       fg_service;
    WakeLockManager         wakelock;
    NotificationHelper      notifications;
    BatteryHelper           battery;
    SafHelper               saf;
    PermissionHelper        permissions;

    std::atomic<bool> initialised{false};
};

static PlatformState g_platform;

/**
 * Initialise the Android platform layer.
 *
 * Must be called from a thread with a valid JNIEnv (typically from
 * the main Activity after native library load).
 */
inline bool platform_init(JNIEnv *env, jobject context) {
    if (g_platform.initialised.load()) return true;
    if (!env || !context) {
        spdlog::error("[android] platform_init: null env/context");
        return false;
    }

    init_class_cache(env);

    auto dev = get_device_info(env);
    spdlog::info("[android] Device: {} {} (Android {}, SDK {})",
                 dev.manufacturer, dev.model,
                 dev.os_version, dev.sdk_version);

    // Check critical permissions
    auto missing = PermissionHelper::missing_permissions(env, context);
    if (!missing.empty()) {
        spdlog::warn("[android] Missing permissions: {} total", missing.size());
        for (auto &p : missing) {
            spdlog::warn("[android]   - {}", p);
        }
    }

    g_platform.initialised.store(true);
    spdlog::info("[android] Platform initialised");
    return true;
}

/**
 * Shut down the Android platform layer gracefully.
 */
inline void platform_shutdown(JNIEnv *env) {
    if (!g_platform.initialised.load()) return;

    g_platform.capture.shutdown(env);
    g_platform.encoder.stop(env);
    g_platform.microphone.stop();
    g_platform.speaker.stop(env);
    g_platform.wakelock.release(env);
    g_platform.fg_service.stop_foreground(env);

    g_platform.initialised.store(false);
    spdlog::info("[android] Platform shutdown complete");
}

} // namespace cppdesk::platform::android

// ===========================================================================
//  JNI_OnLoad — library entry point
// ===========================================================================

/**
 * Called when the native library is loaded by the Android runtime.
 * We cache the JavaVM pointer here for later use across threads.
 */
extern "C" JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM *vm, void * /* reserved */) {
    cppdesk::platform::android::g_jvm = vm;

    // Optionally initialise the class cache early
    JNIEnv *env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) == JNI_OK
        && env) {
        cppdesk::platform::android::init_class_cache(env);
    }

    spdlog::info("[android] Native library loaded (JNI_OnLoad)");
    return JNI_VERSION_1_6;
}

#endif // __ANDROID__
