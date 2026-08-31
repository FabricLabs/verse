#include "platform_android.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#ifdef __ANDROID__
#include <android/log.h>
#include <sys/system_properties.h>

static JavaVM *g_vm = NULL;
static jobject g_activity = NULL; // Global ref to Activity when provided
static AAssetManager *g_asset_manager = NULL;

void android_platform_init(JavaVM *jvm, jobject activity)
{
    g_vm = jvm;
    if (activity)
    {
        // Note: We assume caller passes a global ref or does not require retention.
        g_activity = activity;
    }
}

JNIEnv *android_get_jni_env(void)
{
    if (!g_vm)
        return NULL;
    JNIEnv *env = NULL;
    if ((*g_vm)->GetEnv(g_vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK)
    {
        if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != 0)
            return NULL;
    }
    return env;
}

AAssetManager *android_get_asset_manager(void)
{
    return g_asset_manager;
}

void android_set_asset_manager(AAssetManager *asset_manager)
{
    g_asset_manager = asset_manager;
}

void android_platform_shutdown(void)
{
    // No-op. If we owned a global ref for g_activity we'd delete it here.
}

int android_is_meta_quest(void)
{
    // Check multiple properties that typically indicate Oculus/Meta devices.
    // These include ro.product.manufacturer, ro.product.brand, ro.product.name.
    char manufacturer[PROP_VALUE_MAX] = {0};
    char brand[PROP_VALUE_MAX] = {0};
    char device[PROP_VALUE_MAX] = {0};
    __system_property_get("ro.product.manufacturer", manufacturer);
    __system_property_get("ro.product.brand", brand);
    __system_property_get("ro.product.device", device);

    // Normalize to lowercase simple check
    for (char *p = manufacturer; *p; ++p) *p = (char)tolower(*p);
    for (char *p = brand; *p; ++p) *p = (char)tolower(*p);
    for (char *p = device; *p; ++p) *p = (char)tolower(*p);

    if (strstr(manufacturer, "oculus") || strstr(manufacturer, "meta")) return 1;
    if (strstr(brand, "oculus") || strstr(brand, "meta")) return 1;
    if (strstr(device, "hollywood") || strstr(device, "eureka") || strstr(device, "seacliff") || strstr(device, "monterey")) return 1; // Quest device codenames
    return 0;
}

void android_log(int level, const char *tag, const char *fmt, ...)
{
    if (!tag) tag = "VERSE";
    va_list args;
    va_start(args, fmt);
    __android_log_vprint(level, tag, fmt, args);
    va_end(args);
}

void *android_asset_read_all(const char *path, int *out_size)
{
    if (out_size) *out_size = 0;
    if (!path)
        return NULL;

    AAssetManager *am = android_get_asset_manager();
    if (am)
    {
        AAsset *asset = AAssetManager_open(am, path, AASSET_MODE_BUFFER);
        if (asset)
        {
            const off_t length = AAsset_getLength(asset);
            if (length <= 0)
            {
                AAsset_close(asset);
                return NULL;
            }
            void *buffer = malloc((size_t)length);
            if (!buffer)
            {
                AAsset_close(asset);
                return NULL;
            }
            const int64_t read_bytes = AAsset_read(asset, buffer, (size_t)length);
            AAsset_close(asset);
            if (read_bytes != length)
            {
                free(buffer);
                return NULL;
            }
            if (out_size) *out_size = (int)length;
            return buffer;
        }
    }

    // Fallback to regular file I/O if asset manager is not available
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long size = ftell(f);
    if (size < 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    void *buf = malloc((size_t)size);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (n != (size_t)size) { free(buf); return NULL; }
    if (out_size) *out_size = (int)size;
    return buf;
}

#else // !__ANDROID__

static void *g_vm = NULL;
static void *g_activity = NULL;
static void *g_asset_manager = NULL;

void android_platform_init(void *jvm, void *activity)
{
    g_vm = jvm;
    g_activity = activity;
}

void *android_get_jni_env(void)
{
    (void)g_vm;
    return NULL;
}

void *android_get_asset_manager(void)
{
    return g_asset_manager;
}

void android_set_asset_manager(void *asset_manager)
{
    g_asset_manager = asset_manager;
}

void android_platform_shutdown(void)
{
}

int android_is_meta_quest(void)
{
    return 0;
}

void android_log(int level, const char *tag, const char *fmt, ...)
{
    (void)level;
    if (!tag) tag = "VERSE";
    fprintf(stderr, "[%s] ", tag);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

static void *read_file_all(const char *path, int *out_size)
{
    if (out_size) *out_size = 0;
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long size = ftell(f);
    if (size < 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    void *buf = malloc((size_t)size);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (n != (size_t)size) { free(buf); return NULL; }
    if (out_size) *out_size = (int)size;
    return buf;
}

void *android_asset_read_all(const char *path, int *out_size)
{
    if (!path)
        return NULL;
    // Try exact path
    void *buf = read_file_all(path, out_size);
    if (buf)
        return buf;
    // Try assets/ prefix as a convenience in desktop builds
    char fallback[1024];
    snprintf(fallback, sizeof(fallback), "assets/%s", path);
    return read_file_all(fallback, out_size);
}

#endif // __ANDROID__


