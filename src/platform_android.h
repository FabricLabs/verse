#ifndef VERSE_PLATFORM_ANDROID_H
#define VERSE_PLATFORM_ANDROID_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

// Public API keeps portability: on non-Android, pointers are opaque (void*).
// On Android builds, we expose typed signatures for convenience.

#ifdef __ANDROID__
#include <jni.h>
#include <android/asset_manager.h>
void android_platform_init(JavaVM *jvm, jobject activity);
JNIEnv *android_get_jni_env(void);
AAssetManager *android_get_asset_manager(void);
void android_set_asset_manager(AAssetManager *asset_manager);
#else
void android_platform_init(void *jvm, void *activity);
void *android_get_jni_env(void);
void *android_get_asset_manager(void);
void android_set_asset_manager(void *asset_manager);
#endif

// Common APIs available on all platforms (no-ops or fallbacks off-Android)
void android_platform_shutdown(void);

// Returns 1 if running on a Meta Quest device family, 0 otherwise (or unknown).
int android_is_meta_quest(void);

// Simple logging helper. On Android this uses logcat; elsewhere it prints to stderr.
// level follows Android priorities when available: 2=VERBOSE, 3=DEBUG, 4=INFO, 5=WARN, 6=ERROR.
void android_log(int level, const char *tag, const char *fmt, ...);

// Read an asset or file fully into a newly allocated buffer.
// On Android, attempts to read from the application's AAssetManager.
// Off-Android, attempts to read from the local filesystem. If the path is
// relative, it will also try "assets/<path>" as a fallback.
// Returns malloc'd buffer (caller must free) and writes size to out_size.
void *android_asset_read_all(const char *path, int *out_size);

#ifdef __cplusplus
}
#endif

#endif // VERSE_PLATFORM_ANDROID_H


