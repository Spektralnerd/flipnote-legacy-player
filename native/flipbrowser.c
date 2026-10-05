/*
 * Flipnote Legacy Player thumbnail browser
 * NativeActivity implementation for Android 2.3 / API 9 and newer.
 *
 * This component is deliberately self-contained. It scans the external
 * storage, silently animates every visible PPM preview, and returns the
 * selected file to the existing Java player through Activity.setResult().
 *
 * Since v1.4.13 ZIP archives are browsed like folders (zip_archive.inc),
 * up to 1000 entries per folder are listed and the last folder is restored.
 * Since v1.4.15 uncaught Java exceptions are written to a crash log
 * (crash_log.inc) before the app closes.
 */
#include <jni.h>
#include <stdint.h>
#include <stddef.h>

/* ---- Small API-9 NDK compatibility declarations ----------------------- */
typedef struct AAssetManager AAssetManager;
typedef struct AInputQueue AInputQueue;
typedef struct AInputEvent AInputEvent;
typedef struct ANativeWindow ANativeWindow;
typedef struct AConfiguration AConfiguration;
typedef struct ARect { int32_t left, top, right, bottom; } ARect;
typedef struct ANativeWindow_Buffer {
    int32_t width;
    int32_t height;
    int32_t stride;
    int32_t format;
    void* bits;
    uint32_t reserved[6];
} ANativeWindow_Buffer;

struct ANativeActivityCallbacks;
typedef struct ANativeActivity {
    struct ANativeActivityCallbacks* callbacks;
    JavaVM* vm;
    JNIEnv* env;
    jobject clazz;
    const char* internalDataPath;
    const char* externalDataPath;
    int32_t sdkVersion;
    void* instance;
    AAssetManager* assetManager;
} ANativeActivity;

typedef struct ANativeActivityCallbacks {
    void (*onStart)(ANativeActivity*);
    void (*onResume)(ANativeActivity*);
    void* (*onSaveInstanceState)(ANativeActivity*, size_t*);
    void (*onPause)(ANativeActivity*);
    void (*onStop)(ANativeActivity*);
    void (*onDestroy)(ANativeActivity*);
    void (*onWindowFocusChanged)(ANativeActivity*, int);
    void (*onNativeWindowCreated)(ANativeActivity*, ANativeWindow*);
    void (*onNativeWindowResized)(ANativeActivity*, ANativeWindow*);
    void (*onNativeWindowRedrawNeeded)(ANativeActivity*, ANativeWindow*);
    void (*onNativeWindowDestroyed)(ANativeActivity*, ANativeWindow*);
    void (*onInputQueueCreated)(ANativeActivity*, AInputQueue*);
    void (*onInputQueueDestroyed)(ANativeActivity*, AInputQueue*);
    void (*onContentRectChanged)(ANativeActivity*, const ARect*);
    void (*onConfigurationChanged)(ANativeActivity*);
    void (*onLowMemory)(ANativeActivity*);
} ANativeActivityCallbacks;

extern void ANativeActivity_finish(ANativeActivity* activity);
extern void ANativeActivity_setWindowFormat(ANativeActivity* activity, int32_t format);
extern int32_t ANativeWindow_lock(ANativeWindow*, ANativeWindow_Buffer*, ARect*);
extern int32_t ANativeWindow_unlockAndPost(ANativeWindow*);
extern int32_t AInputQueue_getEvent(AInputQueue*, AInputEvent**);
extern int32_t AInputQueue_preDispatchEvent(AInputQueue*, AInputEvent*);
extern void AInputQueue_finishEvent(AInputQueue*, AInputEvent*, int handled);
extern int32_t AInputEvent_getType(const AInputEvent*);
extern int32_t AKeyEvent_getAction(const AInputEvent*);
extern int32_t AKeyEvent_getKeyCode(const AInputEvent*);
extern int32_t AMotionEvent_getAction(const AInputEvent*);
extern int64_t AMotionEvent_getEventTime(const AInputEvent*);
extern float AMotionEvent_getX(const AInputEvent*, size_t pointer_index);
extern float AMotionEvent_getY(const AInputEvent*, size_t pointer_index);

#include "android_optional.inc"

/* Minimal libc declarations matching 32-bit bionic. Host-test builds use
   the platform headers so the exact same decoder code can be regression
   tested outside Android. */
#if defined(FLIPBROWSER_HOST_TEST)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#else
typedef struct __sFILE FILE;
typedef struct __dirstream DIR;
struct dirent {
    uint64_t d_ino;
    int64_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[256];
};
extern void* malloc(size_t);
extern void free(void*);
extern void* memset(void*, int, size_t);
extern void* memcpy(void*, const void*, size_t);
extern size_t strlen(const char*);
extern int strcmp(const char*, const char*);
extern char* strncpy(char*, const char*, size_t);
extern FILE* fopen(const char*, const char*);
extern size_t fread(void*, size_t, size_t, FILE*);
extern size_t fwrite(const void*, size_t, size_t, FILE*);
extern int fseek(FILE*, long, int);
extern long ftell(FILE*);
extern int fclose(FILE*);
extern int rename(const char*, const char*);
extern int unlink(const char*);
extern int rmdir(const char*);
extern int mkdir(const char*, unsigned int);
/* Archives may exceed 2 GB: 64-bit positions via lseek64 (bionic since
   API 1), while the 32-bit stdio seek stays in use for small files only. */
extern int open(const char*, int, ...);
extern int close(int);
extern long read(int, void*, size_t);
extern long long lseek64(int, long long, int);
extern DIR* opendir(const char*);
extern struct dirent* readdir(DIR*);
extern int closedir(DIR*);
extern void qsort(void*, size_t, size_t, int (*)(const void*, const void*));
extern int usleep(unsigned int);
struct timeval { long tv_sec; long tv_usec; };
extern int gettimeofday(struct timeval*, void*);
typedef unsigned long pthread_t;
extern int pthread_create(pthread_t*, const void*, void* (*)(void*), void*);
extern int pthread_join(pthread_t, void**);
#endif

#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_END
#define SEEK_END 2
#endif
#ifndef DT_DIR
#define DT_DIR 4
#endif
#ifndef DT_REG
#define DT_REG 8
#endif
#define WINDOW_FORMAT_RGBX_8888 2
#define AINPUT_EVENT_TYPE_KEY 1
#define AINPUT_EVENT_TYPE_MOTION 2
#define AKEY_EVENT_ACTION_DOWN 0
#define AMOTION_EVENT_ACTION_MASK 0xff
#define AMOTION_EVENT_ACTION_DOWN 0
#define AMOTION_EVENT_ACTION_UP 1
#define AMOTION_EVENT_ACTION_MOVE 2
#define AMOTION_EVENT_ACTION_CANCEL 3
#define AKEYCODE_BACK 4
#define AKEYCODE_DPAD_UP 19
#define AKEYCODE_DPAD_DOWN 20
#define AKEYCODE_DPAD_LEFT 21
#define AKEYCODE_DPAD_RIGHT 22
#define AKEYCODE_DPAD_CENTER 23
#define AKEYCODE_ENTER 66

#define MAX_ITEMS 1000
#define MAX_ITEMS_TEXT "MAXIMAL 1000 EINTRAEGE ANGEZEIGT"
#define MAX_NAME 112
#define MAX_PATH 640
#define THUMB_W 64
#define THUMB_H 48
#define THUMB_PIXELS (THUMB_W * THUMB_H)
#define PREVIEW_CACHE_LIMIT 8
#define PPM_THUMB_OFFSET 0xA0
#define PPM_THUMB_BYTES 1536
#define PPM_WIDTH 256
#define PPM_HEIGHT 192
#define PPM_ROW_BYTES (PPM_WIDTH / 8)
#define PPM_LAYER_BYTES (PPM_ROW_BYTES * PPM_HEIGHT)
#define PPM_LAYER_PAIR_BYTES (PPM_LAYER_BYTES * 2)
#define PPM_ANIMATION_OFFSET 0x6A0
#define PPM_MIN_BYTES 0x6A8
#define PPM_MAX_PREVIEW_BYTES (16 * 1024 * 1024)
#define MAX_CATCHUP_FRAMES 3

/* ---- Compiler support for ARMv5 without a bundled compiler runtime ----- */
#if defined(__arm__)
static uint32_t udiv32(uint32_t n, uint32_t d) {
    uint32_t q = 0, bit = 1;
    if (d == 0) return 0xffffffffu;
    while ((d & 0x80000000u) == 0 && d < n) {
        d <<= 1;
        bit <<= 1;
    }
    while (bit) {
        if (n >= d) { n -= d; q |= bit; }
        d >>= 1;
        bit >>= 1;
    }
    return q;
}
uint32_t __udivsi3(uint32_t n, uint32_t d) { return udiv32(n, d); }
int32_t __divsi3(int32_t n, int32_t d) {
    uint32_t un, ud, q;
    int neg = 0;
    if (n < 0) { un = (uint32_t)(-(n + 1)) + 1u; neg ^= 1; } else un = (uint32_t)n;
    if (d < 0) { ud = (uint32_t)(-(d + 1)) + 1u; neg ^= 1; } else ud = (uint32_t)d;
    q = udiv32(un, ud);
    return neg ? -(int32_t)q : (int32_t)q;
}
void* __aeabi_memcpy(void* d, const void* s, size_t n) { return memcpy(d, s, n); }
void* __aeabi_memcpy4(void* d, const void* s, size_t n) { return memcpy(d, s, n); }
void* __aeabi_memcpy8(void* d, const void* s, size_t n) { return memcpy(d, s, n); }
void* __aeabi_memset(void* d, size_t n, int c) { return memset(d, c, n); }
void* __aeabi_memset4(void* d, size_t n, int c) { return memset(d, c, n); }
void* __aeabi_memset8(void* d, size_t n, int c) { return memset(d, c, n); }
void* __aeabi_memclr(void* d, size_t n) { return memset(d, 0, n); }
void* __aeabi_memclr4(void* d, size_t n) { return memset(d, 0, n); }
void* __aeabi_memclr8(void* d, size_t n) { return memset(d, 0, n); }

/* Return quotient in r0 and remainder in r1 as required by the ARM EABI. */
__asm__(
".global __aeabi_uidiv\n"
"__aeabi_uidiv:\n"
"  b __udivsi3\n"
".global __aeabi_idiv\n"
"__aeabi_idiv:\n"
"  b __divsi3\n"
".global __aeabi_uidivmod\n"
"__aeabi_uidivmod:\n"
"  push {r4, r5, lr}\n"
"  mov r4, r0\n"
"  mov r5, r1\n"
"  bl __udivsi3\n"
"  mul r1, r0, r5\n"
"  sub r1, r4, r1\n"
"  pop {r4, r5, pc}\n"
".global __aeabi_idivmod\n"
"__aeabi_idivmod:\n"
"  push {r4, r5, lr}\n"
"  mov r4, r0\n"
"  mov r5, r1\n"
"  bl __divsi3\n"
"  mul r1, r0, r5\n"
"  sub r1, r4, r1\n"
"  pop {r4, r5, pc}\n"
);
#endif

static int try_lock(volatile int* lock) {
#if defined(__arm__)
    int old;
    int one = 1;
    __asm__ volatile("swp %0, %2, [%1]" : "=&r"(old) : "r"(lock), "r"(one) : "memory");
    return old;
#elif defined(__aarch64__)
    int old, failed;
    __asm__ volatile(
        "1: ldaxr %w0, [%2]\n"
        "   cbnz %w0, 2f\n"
        "   stlxr %w1, %w3, [%2]\n"
        "   cbnz %w1, 1b\n"
        "2:"
        : "=&r"(old), "=&r"(failed)
        : "r"(lock), "r"(1)
        : "memory");
    return old;
#elif defined(__i386__) || defined(__x86_64__)
    int value = 1;
    __asm__ volatile("xchg %0, %1" : "+r"(value), "+m"(*lock) : : "memory");
    return value;
#else
    int old = *lock;
    *lock = 1;
    return old;
#endif
}

static void release_lock(volatile int* lock) {
#if defined(__aarch64__)
    __asm__ volatile("stlr wzr, [%0]" : : "r"(lock) : "memory");
#elif defined(__arm__)
    __asm__ volatile("" : : : "memory");
    *lock = 0;
    __asm__ volatile("" : : : "memory");
#else
    __asm__ volatile("" : : : "memory");
    *lock = 0;
#endif
}

/* ---- Silent PPM preview animation model ------------------------------- */
typedef struct PpmAnimation {
    unsigned char* data;
    int data_size;
    int frame_count;
    int frame_data_offset;
    int decoded_frame;
    int current_header;
    int frame_interval_ms;
    uint32_t next_due_ms;
    unsigned char* layer_memory;
    unsigned char* current_layers;
    unsigned char* previous_layers;
} PpmAnimation;

static void destroy_animation(PpmAnimation* animation);

/* ---- Browser model ----------------------------------------------------- */
struct ZipArchive;

typedef struct BrowserItem {
    char name[MAX_NAME];
    char path[MAX_PATH];  /* file system path, or a virtual path inside a ZIP */
    int32_t size;
    int is_access;
    int is_dir;
    int is_parent;
    int is_system;
    int is_archive;       /* a .zip file shown as an openable folder */
    int in_zip;           /* a PPM entry inside the currently opened archive */
    int zip_entry;        /* index into the archive entry table when in_zip */
    int thumb_state; /* 0 not loaded, 1 loaded, -1 invalid */
    uint32_t* thumb;
    PpmAnimation* animation;
    uint32_t stamp;
} BrowserItem;

typedef struct BrowserState {
    ANativeActivity* activity;
    volatile int running;
    volatile int draw_busy;
    volatile int input_busy;
    volatile int redraw;
    volatile ANativeWindow* window;
    volatile AInputQueue* input;
    pthread_t thread;

    char directory[MAX_PATH];
    char message[160];
    BrowserItem items[MAX_ITEMS];
    int item_count;
    int selected;
    int scroll;
    int permission_pending;
    int permission_poll;
    volatile int resume_check;
    int manage_access;
    int manage_settings_open;
    int finishing;
    volatile int paused;
    volatile int trim_requested;
    uint32_t stamp;
    int thumb_count;

    /* Optional one-shot return target supplied by the player. */
    int restore_pending;
    int reveal_selected;
    char restore_directory[MAX_PATH];
    char restore_path[MAX_PATH];

    /* ZIP archives as folders and the persistent last folder (v1.4.13). */
    struct ZipArchive* zip;        /* index of the archive named in directory */
    char cache_root[MAX_PATH];     /* <app cache>/zip, empty when unavailable */
    char last_directory[MAX_PATH]; /* stored start folder, may be virtual */
    int directory_dirty;           /* directory changed, preference not saved */
    int cache_checked;

    int animate_previews;
    int menu_open;
    int touching;
    int suppress_tap;
    int fling_velocity; /* pixels per second */
    int fling_position_q8;
    uint32_t fling_last_ms;
    uint32_t touch_last_ms;
    uint32_t sample_time[8];
    int sample_y[8];
    int sample_count;

    int touch_down_x;
    int touch_down_y;
    int touch_last_y;
    int touch_moved;
} BrowserState;

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int iclamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static int float_bits_to_int(float value) {
    union { float f; uint32_t u; } x;
    int sign, exp;
    uint32_t mant, v;
    x.f = value;
    sign = (int)(x.u >> 31);
    exp = (int)((x.u >> 23) & 0xffu) - 127;
    mant = (x.u & 0x7fffffu) | 0x800000u;
    if (exp < 0) return 0;
    if (exp > 30) return sign ? -2147483647 : 2147483647;
    if (exp >= 23) v = mant << (exp - 23);
    else v = mant >> (23 - exp);
    return sign ? -(int)v : (int)v;
}

static void copy_text(char* dst, const char* src, int cap) {
    int i = 0;
    if (!dst || cap <= 0) return;
    if (src) while (i < cap - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static int ascii_lower(int c) {
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static int name_compare_ci(const char* a, const char* b) {
    int ca, cb;
    while (*a && *b) {
        ca = ascii_lower((unsigned char)*a++);
        cb = ascii_lower((unsigned char)*b++);
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    return *a ? 1 : *b ? -1 : 0;
}

static int ends_ppm(const char* name) {
    size_t n = strlen(name);
    if (n < 4) return 0;
    return name[n - 4] == '.'
        && ascii_lower((unsigned char)name[n - 3]) == 'p'
        && ascii_lower((unsigned char)name[n - 2]) == 'p'
        && ascii_lower((unsigned char)name[n - 1]) == 'm';
}

static void join_path(char* out, const char* dir, const char* name) {
    int p = 0, i = 0;
    while (p < MAX_PATH - 1 && dir[p]) { out[p] = dir[p]; p++; }
    if (p > 0 && out[p - 1] != '/' && p < MAX_PATH - 1) out[p++] = '/';
    while (p < MAX_PATH - 1 && name[i]) out[p++] = name[i++];
    out[p] = 0;
}

static void parent_path(const char* path, char* out) {
    int n = (int)strlen(path);
    while (n > 1 && path[n - 1] == '/') n--;
    while (n > 1 && path[n - 1] != '/') n--;
    if (n > 1) n--;
    if (n <= 0) { out[0] = '/'; out[1] = 0; return; }
    if (n == 1 && path[0] == '/') { out[0] = '/'; out[1] = 0; return; }
    if (n >= MAX_PATH) n = MAX_PATH - 1;
    memcpy(out, path, (size_t)n);
    out[n] = 0;
}

static int path_is_root(const char* path) {
    return path[0] == '/' && path[1] == 0;
}

static int item_cmp(const void* av, const void* bv) {
    const BrowserItem* a = (const BrowserItem*)av;
    const BrowserItem* b = (const BrowserItem*)bv;
    if (a->is_access != b->is_access) return a->is_access ? -1 : 1;
    if (a->is_system != b->is_system) return a->is_system ? -1 : 1;
    if (a->is_parent != b->is_parent) return a->is_parent ? -1 : 1;
    if (a->is_dir != b->is_dir) return a->is_dir ? -1 : 1;
    if (a->is_archive != b->is_archive) return a->is_archive ? 1 : -1;
    return name_compare_ci(a->name, b->name);
}

static void free_thumbnails(BrowserState* s) {
    int i;
    for (i = 0; i < s->item_count; i++) {
        if (s->items[i].animation) destroy_animation(s->items[i].animation);
        s->items[i].animation = 0;
        if (s->items[i].thumb) free(s->items[i].thumb);
        s->items[i].thumb = 0;
        s->items[i].thumb_state = 0;
    }
    s->thumb_count = 0;
}

static int directory_readable(const char* path) {
    DIR* d = opendir(path);
    if (!d) return 0;
    closedir(d);
    return 1;
}

static int file_size32(const char* path) {
    FILE* f = fopen(path, "rb");
    long n;
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    n = ftell(f);
    fclose(f);
    if (n < 0) return -1;
    if (n > 0x7fffffffL) return 0x7fffffff;
    return (int)n;
}

static uint32_t monotonicish_millis(void) {
    struct timeval tv;
    if (gettimeofday(&tv, 0) != 0) return 0;
    return (uint32_t)(tv.tv_sec * 1000L + tv.tv_usec / 1000L);
}

static int range_ok(int size, int offset, int length) {
    return offset >= 0 && length >= 0 && offset <= size - length;
}

static int read_u16le_at(const unsigned char* data, int size, int offset, int* out) {
    if (!range_ok(size, offset, 2)) return 0;
    *out = (int)data[offset] | ((int)data[offset + 1] << 8);
    return 1;
}

static int read_u32le_at(const unsigned char* data, int size, int offset, uint32_t* out) {
    if (!range_ok(size, offset, 4)) return 0;
    *out = (uint32_t)data[offset]
         | ((uint32_t)data[offset + 1] << 8)
         | ((uint32_t)data[offset + 2] << 16)
         | ((uint32_t)data[offset + 3] << 24);
    return 1;
}

static int time_reached(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}

static void set_message(BrowserState* s, const char* text) {
    copy_text(s->message, text, (int)sizeof(s->message));
}

#include "zip_archive.inc"

/* Keeps the MAX_ITEMS entries that sort first even when a folder holds more,
   so a truncated listing is always the sorted beginning of the folder and
   not whatever readdir() happened to return first. */
static int scan_insert(BrowserState* s, int* count, const BrowserItem* candidate,
                       int* max_index) {
    int i;
    if (*count < MAX_ITEMS) {
        s->items[(*count)++] = *candidate;
        *max_index = -1;
        return 1;
    }
    if (*max_index < 0) {
        *max_index = 0;
        for (i = 1; i < *count; i++)
            if (item_cmp(&s->items[i], &s->items[*max_index]) > 0) *max_index = i;
    }
    if (item_cmp(candidate, &s->items[*max_index]) >= 0) return 0;
    s->items[*max_index] = *candidate;
    *max_index = -1;
    return 1;
}

static int browser_open_zip(BrowserState* s, const char* zip_path) {
    if (s->zip && strcmp(s->zip->path, zip_path) == 0) return 1;
    if (s->zip) zip_archive_free(s->zip);
    s->zip = zip_archive_open(zip_path);
    return s->zip != 0;
}

static int folder_already_listed(BrowserState* s, int count, const char* folder) {
    int i;
    for (i = 0; i < count; i++) {
        const BrowserItem* it = &s->items[i];
        if (it->is_dir && !it->is_parent && !it->is_archive
                && strcmp(it->name, folder) == 0) return 1;
    }
    return 0;
}

/* Lists the sub folders and PPM entries directly below `inner` of the
   opened archive. Entries are grouped by folder in almost every archive,
   so remembering the previous folder name avoids most duplicate checks. */
static int scan_zip_entries(BrowserState* s, const char* inner, int* count,
                            int* dropped, int* max_index, int* unsupported) {
    ZipArchive* zip = s->zip;
    char prefix[MAX_PATH];
    char last_folder[MAX_NAME];
    BrowserItem candidate;
    int prefix_len, i, content = 0;

    copy_text(prefix, inner, MAX_PATH);
    prefix_len = (int)strlen(prefix);
    if (prefix_len > 0 && prefix_len < MAX_PATH - 1) {
        prefix[prefix_len++] = '/';
        prefix[prefix_len] = 0;
    }
    last_folder[0] = 0;
    for (i = 0; i < zip->entry_count; i++) {
        const char* name = zip_entry_name(zip, i);
        const char* rest;
        int slash = -1, j;
        if (!text_starts_with(name, prefix)) continue;
        rest = name + prefix_len;
        for (j = 0; rest[j]; j++) if (rest[j] == '/') { slash = j; break; }
        memset(&candidate, 0, sizeof(candidate));
        if (slash >= 0) {
            char folder[MAX_NAME];
            if (slash >= MAX_NAME) continue;
            memcpy(folder, rest, (size_t)slash);
            folder[slash] = 0;
            if (strcmp(folder, last_folder) == 0) continue;
            copy_text(last_folder, folder, MAX_NAME);
            if (folder_already_listed(s, *count, folder)) continue;
            if (strlen(zip->path) + 1 + (size_t)prefix_len + (size_t)slash >= MAX_PATH) continue;
            candidate.is_dir = 1;
            copy_text(candidate.name, folder, MAX_NAME);
            join_path(candidate.path, zip->path, prefix);
            copy_text(candidate.path + strlen(candidate.path), folder,
                      MAX_PATH - (int)strlen(candidate.path));
        } else {
            if (strlen(zip->path) + 1 + strlen(name) >= MAX_PATH) continue;
            candidate.in_zip = 1;
            candidate.zip_entry = i;
            candidate.size = (int32_t)zip->entries[i].locator.uncompressed_size;
            copy_text(candidate.name, rest, MAX_NAME);
            join_path(candidate.path, zip->path, name);
            if (!zip_locator_supported(&zip->entries[i].locator)) (*unsupported)++;
        }
        content++;
        if (!scan_insert(s, count, &candidate, max_index)) (*dropped)++;
    }
    return content;
}

static int scan_directory(BrowserState* s, const char* path) {
    DIR* dir;
    struct dirent* ent;
    char full[MAX_PATH];
    char requested[MAX_PATH];
    char zip_path[MAX_PATH];
    char inner[MAX_PATH];
    BrowserItem* it;
    BrowserItem candidate;
    int count = 0;
    int content_count = 0;
    int dropped = 0;
    int unsupported = 0;
    int max_index = -1;
    int in_archive;

    /* The requested path can point into the current item array. Keep a copy
       before clearing that array for the next directory. */
    copy_text(requested, path, MAX_PATH);
    path = requested;
    free_thumbnails(s);
    memset(s->items, 0, sizeof(s->items));
    s->item_count = 0;
    s->selected = 0;
    s->scroll = 0;
    s->fling_velocity = 0;
    set_message(s, "");

    /* Android 13 and newer no longer grant useful direct path access through
       READ_EXTERNAL_STORAGE. Keep the legacy browser intact for old devices,
       but expose an explicit one-time special-access shortcut on new ones. */
    if (s->activity->sdkVersion >= 33 && !s->manage_access && count < MAX_ITEMS) {
        it = &s->items[count++];
        copy_text(it->name, "DATEIZUGRIFF", MAX_NAME);
        it->is_access = 1;
    }

    /* Always retain the privacy-preserving Android picker as a fallback on
       systems that support the Storage Access Framework. */
    if (s->activity->sdkVersion >= 19 && count < MAX_ITEMS) {
        it = &s->items[count++];
        copy_text(it->name, "SYSTEM-DATEIEN", MAX_NAME);
        it->is_system = 1;
    }

    in_archive = zip_split_path(path, zip_path, inner);
    if (in_archive) {
        /* The parent card comes first so an unreadable archive still has
           a way back on screen. */
        it = &s->items[count++];
        copy_text(it->name, "..", MAX_NAME);
        parent_path(path, it->path);
        it->is_dir = 1;
        it->is_parent = 1;
        if (!browser_open_zip(s, zip_path)) {
            copy_text(s->directory, path, MAX_PATH);
            s->item_count = count;
            set_message(s, "ZIP NICHT LESBAR - KEIN ZIP ODER DEFEKT");
            s->redraw = 1;
            return 0;
        }
        content_count = scan_zip_entries(s, inner, &count, &dropped, &max_index, &unsupported);
    } else {
        dir = opendir(path);

        if (!dir) {
            copy_text(s->directory, path, MAX_PATH);
            s->item_count = count;
            if (s->activity->sdkVersion >= 33 && !s->manage_access)
                set_message(s, "DATEIZUGRIFF ANTIPPEN UND ALLE DATEIEN ERLAUBEN");
            else
                set_message(s, "ORDNER NICHT LESBAR - SPEICHERBERECHTIGUNG PRUEFEN");
            s->redraw = 1;
            return 0;
        }

        if (!path_is_root(path)) {
            it = &s->items[count++];
            copy_text(it->name, "..", MAX_NAME);
            parent_path(path, it->path);
            it->is_dir = 1;
            it->is_parent = 1;
        }

        while ((ent = readdir(dir)) != 0) {
            int is_dir = 0;
            if (ent->d_name[0] == '.' && (ent->d_name[1] == 0
                    || (ent->d_name[1] == '.' && ent->d_name[2] == 0))) continue;
            join_path(full, path, ent->d_name);
            if (ent->d_type == DT_DIR) {
                is_dir = 1;
            } else if (ent->d_type != DT_REG) {
                DIR* test = opendir(full);
                if (test) { is_dir = 1; closedir(test); }
            }
            if (!is_dir && !ends_ppm(ent->d_name) && !ends_zip(ent->d_name)) continue;
            memset(&candidate, 0, sizeof(candidate));
            copy_text(candidate.name, ent->d_name, MAX_NAME);
            copy_text(candidate.path, full, MAX_PATH);
            if (is_dir) {
                candidate.is_dir = 1;
            } else if (ends_zip(ent->d_name)) {
                /* Archives open like folders; sorted after real folders. */
                candidate.is_dir = 1;
                candidate.is_archive = 1;
            } else {
                candidate.size = file_size32(full);
            }
            content_count++;
            if (!scan_insert(s, &count, &candidate, &max_index)) dropped++;
        }
        closedir(dir);
    }

    if (count > 1) qsort(s->items, (size_t)count, sizeof(BrowserItem), item_cmp);
    s->item_count = count;
    copy_text(s->directory, path, MAX_PATH);
    s->directory_dirty = 1;
    if (s->activity->sdkVersion >= 33 && !s->manage_access)
        set_message(s, "DATEIEN AUSGEBLENDET - DATEIZUGRIFF ANTIPPEN");
    else if (content_count == 0)
        set_message(s, in_archive ? "ZIP: KEINE ORDNER ODER PPM-DATEIEN"
                                  : "KEINE ORDNER ODER PPM-DATEIEN");
    else if (dropped > 0) set_message(s, MAX_ITEMS_TEXT);
    else if (in_archive && s->zip->truncated)
        set_message(s, "ZIP-INDEX VOLL - NICHT ALLE EINTRAEGE GELISTET");
    else if (unsupported > 0)
        set_message(s, "ZIP: EINTRAEGE MIT VERSCHLUESSELUNG/METHODE OHNE SUPPORT");
    s->redraw = 1;
    return 1;
}

/* Directories and archives count as readable browser targets. */
static int browser_target_readable(const char* path) {
    char zip_path[MAX_PATH];
    char inner[MAX_PATH];
    if (path[0] != '/') return 0;
    if (zip_split_path(path, zip_path, inner)) return 1;
    return directory_readable(path);
}

/* Folder inside the opened archive that the current listing shows. */
static const char* browser_zip_inner(BrowserState* s) {
    const char* inner;
    if (!s->zip || !text_starts_with(s->directory, s->zip->path)) return "";
    inner = s->directory + strlen(s->zip->path);
    while (*inner == '/') inner++;
    return inner;
}

static const ZipLocator* browser_item_locator(BrowserState* s, const BrowserItem* it) {
    if (!it->in_zip || !s->zip || it->zip_entry < 0
            || it->zip_entry >= s->zip->entry_count) return 0;
    return &s->zip->entries[it->zip_entry].locator;
}

/* Cache file that holds (or will hold) the extracted copy of a ZIP item. */
static int browser_item_cache_path(BrowserState* s, const BrowserItem* it, char* out) {
    out[0] = 0;
    if (!browser_item_locator(s, it) || !s->cache_root[0]) return 0;
    return zip_cache_path(s->cache_root, s->zip->path, browser_zip_inner(s), it->name, out);
}

/* ---- PPM thumbnail decoding ------------------------------------------- */
static const uint32_t ppm_palette[16] = {
    0xffffff, 0x000000, 0xffffff, 0x9c9c9c,
    0xff4844, 0xc8514f, 0xffadac, 0x00ff00,
    0x4840ff, 0x514fb8, 0xadabff, 0x00ff00,
    0xb657b7, 0x00ff00, 0x00ff00, 0x00ff00
};

#define PPM_HEAD_BYTES (PPM_THUMB_OFFSET + PPM_THUMB_BYTES)

/* Decodes the embedded 64x48 thumbnail from the first PPM_HEAD_BYTES of a
   file, whether they came from disk or straight out of a ZIP entry. */
static int decode_thumbnail_bytes(const unsigned char* head, int size, uint32_t* out) {
    const unsigned char* data;
    int tx, ty, row, pair, pos = 0;
    if (size < PPM_HEAD_BYTES || head[0] != 'P' || head[1] != 'A'
            || head[2] != 'R' || head[3] != 'A') return 0;
    data = head + PPM_THUMB_OFFSET;
    for (ty = 0; ty < 6; ty++) {
        for (tx = 0; tx < 8; tx++) {
            for (row = 0; row < 8; row++) {
                for (pair = 0; pair < 4; pair++) {
                    unsigned char v = data[pos++];
                    int x = tx * 8 + pair * 2;
                    int y = ty * 8 + row;
                    out[y * THUMB_W + x] = ppm_palette[v & 15];
                    out[y * THUMB_W + x + 1] = ppm_palette[(v >> 4) & 15];
                }
            }
        }
    }
    return 1;
}

static int decode_thumbnail(const char* path, uint32_t* out) {
    FILE* f;
    unsigned char head[PPM_HEAD_BYTES];
    size_t got;
    f = fopen(path, "rb");
    if (!f) return 0;
    got = fread(head, 1, PPM_HEAD_BYTES, f);
    fclose(f);
    return decode_thumbnail_bytes(head, (int)got, out);
}

/* ---- Full PPM frame decoding for silent animated previews ------------- */
typedef struct PpmCursor {
    const unsigned char* data;
    int size;
    int position;
} PpmCursor;

static int cursor_u8(PpmCursor* cursor, int* out) {
    if (!range_ok(cursor->size, cursor->position, 1)) return 0;
    *out = cursor->data[cursor->position++];
    return 1;
}

static int cursor_i8(PpmCursor* cursor, int* out) {
    int value;
    if (!cursor_u8(cursor, &value)) return 0;
    *out = value > 127 ? value - 256 : value;
    return 1;
}

static int cursor_u32be(PpmCursor* cursor, uint32_t* out) {
    const unsigned char* p;
    if (!range_ok(cursor->size, cursor->position, 4)) return 0;
    p = cursor->data + cursor->position;
    *out = ((uint32_t)p[0] << 24)
         | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8)
         | (uint32_t)p[3];
    cursor->position += 4;
    return 1;
}

static int animation_frame_offset(PpmAnimation* animation, int frame, int* out) {
    uint32_t relative;
    int table_offset;
    if (!animation || frame < 0 || frame >= animation->frame_count) return 0;
    table_offset = PPM_ANIMATION_OFFSET + 8 + frame * 4;
    if (!read_u32le_at(animation->data, animation->data_size,
                       table_offset, &relative)) return 0;
    if (animation->frame_data_offset < 0
            || animation->frame_data_offset > animation->data_size
            || relative > (uint32_t)(animation->data_size - animation->frame_data_offset))
        return 0;
    *out = animation->frame_data_offset + (int)relative;
    return range_ok(animation->data_size, *out, 1);
}

static void animation_reset(PpmAnimation* animation) {
    if (!animation || !animation->layer_memory) return;
    memset(animation->layer_memory, 0, PPM_LAYER_PAIR_BYTES * 2);
    animation->current_layers = animation->layer_memory;
    animation->previous_layers = animation->layer_memory + PPM_LAYER_PAIR_BYTES;
    animation->decoded_frame = -1;
    animation->current_header = 0;
}

static unsigned char translated_row_byte(const unsigned char* row,
                                         int output_byte, int translate_x) {
    int source_bit = output_byte * 8 - translate_x;
    int source_byte;
    int shift;
    unsigned int pair = 0;
    if (source_bit >= 0) source_byte = source_bit / 8;
    else source_byte = -((-source_bit + 7) / 8);
    shift = source_bit - source_byte * 8;
    if (source_byte >= 0 && source_byte < PPM_ROW_BYTES)
        pair |= row[source_byte];
    if (source_byte + 1 >= 0 && source_byte + 1 < PPM_ROW_BYTES)
        pair |= (unsigned int)row[source_byte + 1] << 8;
    return (unsigned char)((pair >> shift) & 0xffu);
}

static int animation_decode_frame(PpmAnimation* animation, int frame) {
    int frame_offset;
    PpmCursor cursor;
    int header, keyframe, move_mode, translate_x = 0, translate_y = 0;
    int type_offsets[2];
    int layer_index, y, chunk_index, value;
    unsigned char* swap;

    if (!animation_frame_offset(animation, frame, &frame_offset)) return 0;
    cursor.data = animation->data;
    cursor.size = animation->data_size;
    cursor.position = frame_offset;
    if (!cursor_u8(&cursor, &header)) return 0;
    keyframe = (header & 0x80) != 0;
    move_mode = (header >> 5) & 3;
    if (move_mode != 0) {
        if (!cursor_i8(&cursor, &translate_x)
                || !cursor_i8(&cursor, &translate_y)) return 0;
    }
    if (!range_ok(cursor.size, cursor.position, 96)) return 0;
    type_offsets[0] = cursor.position;
    type_offsets[1] = cursor.position + 48;
    cursor.position += 96;

    /* Swap the two compact layer pairs. The old current frame becomes the
       immutable previous frame while the other pair is cleared for output. */
    swap = animation->previous_layers;
    animation->previous_layers = animation->current_layers;
    animation->current_layers = swap;
    memset(animation->current_layers, 0, PPM_LAYER_PAIR_BYTES);

    for (layer_index = 0; layer_index < 2; layer_index++) {
        unsigned char* layer = animation->current_layers
                             + layer_index * PPM_LAYER_BYTES;
        const unsigned char* line_types = animation->data + type_offsets[layer_index];
        for (y = 0; y < PPM_HEIGHT; y++) {
            int line_type = (line_types[y >> 2] >> ((y & 3) * 2)) & 3;
            unsigned char* row = layer + y * PPM_ROW_BYTES;
            if (line_type == 0) continue;
            if (line_type == 1 || line_type == 2) {
                uint32_t usage;
                if (line_type == 2) memset(row, 0xff, PPM_ROW_BYTES);
                if (!cursor_u32be(&cursor, &usage)) return 0;
                for (chunk_index = 0; chunk_index < PPM_ROW_BYTES; chunk_index++) {
                    if ((usage & (0x80000000u >> chunk_index)) != 0) {
                        if (!cursor_u8(&cursor, &value)) return 0;
                        row[chunk_index] = (unsigned char)value;
                    }
                }
            } else {
                if (!range_ok(cursor.size, cursor.position, PPM_ROW_BYTES)) return 0;
                memcpy(row, cursor.data + cursor.position, PPM_ROW_BYTES);
                cursor.position += PPM_ROW_BYTES;
            }
        }
    }

    if (!keyframe) {
        for (layer_index = 0; layer_index < 2; layer_index++) {
            unsigned char* layer = animation->current_layers
                                 + layer_index * PPM_LAYER_BYTES;
            const unsigned char* previous = animation->previous_layers
                                          + layer_index * PPM_LAYER_BYTES;
            for (y = 0; y < PPM_HEIGHT; y++) {
                int source_y = y - translate_y;
                unsigned char* row = layer + y * PPM_ROW_BYTES;
                if (source_y >= 0 && source_y < PPM_HEIGHT) {
                    const unsigned char* source_row = previous
                                                    + source_y * PPM_ROW_BYTES;
                    for (chunk_index = 0; chunk_index < PPM_ROW_BYTES; chunk_index++)
                        row[chunk_index] ^= translated_row_byte(
                                source_row, chunk_index, translate_x);
                }
            }
        }
    }

    animation->decoded_frame = frame;
    animation->current_header = header;
    return 1;
}

static int layer_pixel(const unsigned char* layer_pair,
                       int layer_index, int x, int y) {
    const unsigned char* layer = layer_pair + layer_index * PPM_LAYER_BYTES;
    return (layer[y * PPM_ROW_BYTES + (x >> 3)] >> (x & 7)) & 1;
}

static void animation_render_thumbnail(PpmAnimation* animation, uint32_t* out) {
    int white_paper = (animation->current_header & 1) != 0;
    uint32_t paper = white_paper ? 0xffffffu : 0x000000u;
    uint32_t inverse = white_paper ? 0x000000u : 0xffffffu;
    uint32_t pens[4];
    uint32_t pen1, pen2;
    int tx, ty, xx, yy;
    pens[0] = inverse;
    pens[1] = inverse;
    pens[2] = 0xff2a2au;
    pens[3] = 0x0a39ffu;
    pen1 = pens[(animation->current_header >> 1) & 3];
    pen2 = pens[(animation->current_header >> 3) & 3];

    for (ty = 0; ty < THUMB_H; ty++) {
        for (tx = 0; tx < THUMB_W; tx++) {
            int top_count = 0;
            int bottom_count = 0;
            int source_x = tx * 4;
            int source_y = ty * 4;
            for (yy = 0; yy < 4; yy++) {
                for (xx = 0; xx < 4; xx++) {
                    int x = source_x + xx;
                    int y = source_y + yy;
                    if (layer_pixel(animation->current_layers, 0, x, y))
                        top_count++;
                    else if (layer_pixel(animation->current_layers, 1, x, y))
                        bottom_count++;
                }
            }
            out[ty * THUMB_W + tx] = top_count == 0 && bottom_count == 0
                ? paper : top_count >= bottom_count ? pen1 : pen2;
        }
    }
}

static void destroy_animation(PpmAnimation* animation) {
    if (!animation) return;
    if (animation->layer_memory) free(animation->layer_memory);
    if (animation->data) free(animation->data);
    free(animation);
}

/* Builds the silent preview from complete PPM bytes. Takes ownership of
   `data` (freed on failure as well). */
static PpmAnimation* create_animation_bytes(unsigned char* data, int file_size,
                                            uint32_t* thumbnail, uint32_t now) {
    static const int frame_intervals[9] = {
        2000, 2000, 1000, 500, 250, 167, 83, 50, 33
    };
    PpmAnimation* animation = 0;
    int frame_count, table_size, speed_index = 5;
    int frame, sound_header_offset;
    uint32_t animation_data_size, relative;

    if (!data) return 0;
    if (file_size < PPM_MIN_BYTES || file_size > PPM_MAX_PREVIEW_BYTES) {
        free(data);
        return 0;
    }
    animation = (PpmAnimation*)malloc(sizeof(PpmAnimation));
    if (!animation) { free(data); return 0; }
    memset(animation, 0, sizeof(PpmAnimation));
    animation->data = data;
    animation->data_size = file_size;

    if (animation->data[0] != 'P' || animation->data[1] != 'A'
            || animation->data[2] != 'R' || animation->data[3] != 'A') goto fail;
    if (!read_u32le_at(animation->data, file_size, 4, &animation_data_size)
            || animation_data_size > 0x7fffffffu
            || !read_u16le_at(animation->data, file_size, 12, &frame_count)) goto fail;
    frame_count += 1;
    if (frame_count < 1 || frame_count > 999
            || !read_u16le_at(animation->data, file_size,
                              PPM_ANIMATION_OFFSET, &table_size)
            || table_size < frame_count * 4
            || (uint32_t)table_size > animation_data_size) goto fail;
    animation->frame_count = frame_count;
    animation->frame_data_offset = PPM_ANIMATION_OFFSET + 8 + table_size;
    if (!range_ok(file_size, animation->frame_data_offset, 1)) goto fail;
    for (frame = 0; frame < frame_count; frame++) {
        if (!read_u32le_at(animation->data, file_size,
                           PPM_ANIMATION_OFFSET + 8 + frame * 4, &relative)
                || relative > (uint32_t)(file_size - animation->frame_data_offset)
                || !range_ok(file_size,
                             animation->frame_data_offset + (int)relative, 1)) goto fail;
    }

    if (animation_data_size <= 0x7fffffffu - PPM_ANIMATION_OFFSET
            && (int)animation_data_size + PPM_ANIMATION_OFFSET
                    <= 0x7fffffff - frame_count - 3) {
        sound_header_offset = (PPM_ANIMATION_OFFSET
                + (int)animation_data_size + frame_count + 3) & ~3;
        if (range_ok(file_size, sound_header_offset, 18)) {
            int playback_code = animation->data[sound_header_offset + 16];
            int candidate = 8 - playback_code;
            if (candidate >= 0 && candidate < 9) speed_index = candidate;
        }
    }
    animation->frame_interval_ms = frame_intervals[speed_index];
    animation->layer_memory = (unsigned char*)malloc(PPM_LAYER_PAIR_BYTES * 2);
    if (!animation->layer_memory) goto fail;
    animation_reset(animation);
    if (!animation_decode_frame(animation, 0)) goto fail;
    animation_render_thumbnail(animation, thumbnail);
    animation->next_due_ms = now + (uint32_t)animation->frame_interval_ms;
    return animation;

fail:
    destroy_animation(animation);
    return 0;
}

static int read_whole_file(const char* path, unsigned char** out_data, int* out_size) {
    FILE* file;
    unsigned char* data;
    int file_size = file_size32(path);
    if (file_size < PPM_MIN_BYTES || file_size > PPM_MAX_PREVIEW_BYTES) return 0;
    data = (unsigned char*)malloc((size_t)file_size);
    if (!data) return 0;
    file = fopen(path, "rb");
    if (!file) { free(data); return 0; }
    if (fread(data, 1, (size_t)file_size, file) != (size_t)file_size) {
        fclose(file);
        free(data);
        return 0;
    }
    fclose(file);
    *out_data = data;
    *out_size = file_size;
    return 1;
}

static PpmAnimation* create_animation(const char* path, uint32_t* thumbnail,
                                      uint32_t now) {
    unsigned char* data = 0;
    int size = 0;
    if (!read_whole_file(path, &data, &size)) return 0;
    return create_animation_bytes(data, size, thumbnail, now);
}

/* Returns 1 when a new frame was rendered, 0 when nothing was due and -1
   when a malformed later frame disabled animation for this one preview. */
static int advance_animation(PpmAnimation* animation, uint32_t* thumbnail,
                             uint32_t now) {
    int changed = 0;
    int steps = 0;
    int overdue;
    if (!animation || animation->frame_count <= 1) return 0;
    overdue = (int32_t)(now - animation->next_due_ms);
    if (overdue > 1000) animation->next_due_ms = now;
    while (time_reached(now, animation->next_due_ms)
            && steps < MAX_CATCHUP_FRAMES) {
        int next_frame = animation->decoded_frame + 1;
        if (next_frame >= animation->frame_count) {
            animation_reset(animation);
            next_frame = 0;
        }
        if (!animation_decode_frame(animation, next_frame)) return -1;
        animation_render_thumbnail(animation, thumbnail);
        animation->next_due_ms += (uint32_t)animation->frame_interval_ms;
        changed = 1;
        steps++;
    }
    if (steps >= MAX_CATCHUP_FRAMES
            && time_reached(now, animation->next_due_ms))
        animation->next_due_ms = now + (uint32_t)animation->frame_interval_ms;
    return changed;
}

/* Each animated preview holds the PPM bytes plus compact frame state, so the
   off-screen cache is intentionally small. Visible cards are never evicted;
   this preserves the no-flicker behaviour from v1.3.9 even on very tall
   displays where dozens of Flipnotes animate at the same time. */
static int evict_thumbnail(BrowserState* s, int protect_first, int protect_last) {
    int i, victim = -1;
    uint32_t oldest = 0xffffffffu;
    if (s->thumb_count < PREVIEW_CACHE_LIMIT) return 1;
    for (i = 0; i < s->item_count; i++) {
        BrowserItem* it = &s->items[i];
        if (i >= protect_first && i <= protect_last) continue;
        if (it->thumb && it->stamp < oldest) {
            oldest = it->stamp;
            victim = i;
        }
    }
    if (victim < 0) return 0;
    if (s->items[victim].animation)
        destroy_animation(s->items[victim].animation);
    s->items[victim].animation = 0;
    free(s->items[victim].thumb);
    s->items[victim].thumb = 0;
    s->items[victim].thumb_state = 0;
    s->thumb_count--;
    return 1;
}

/* Static preview of a ZIP entry: only the header and thumbnail bytes are
   inflated, which keeps large archives responsive on slow devices. */
static int decode_zip_thumbnail(BrowserState* s, const BrowserItem* it, uint32_t* out) {
    unsigned char head[PPM_HEAD_BYTES];
    const ZipLocator* loc;
    if (!s->zip || it->zip_entry < 0 || it->zip_entry >= s->zip->entry_count) return 0;
    loc = &s->zip->entries[it->zip_entry].locator;
    if (loc->uncompressed_size < (uint32_t)PPM_HEAD_BYTES) return 0;
    if (!zip_read_locator(s->zip->path, loc, head, PPM_HEAD_BYTES, 0)) return 0;
    return decode_thumbnail_bytes(head, PPM_HEAD_BYTES, out);
}

static PpmAnimation* create_zip_animation(BrowserState* s, const BrowserItem* it,
                                          uint32_t* thumbnail, uint32_t now) {
    unsigned char* data = 0;
    int size = 0;
    if (!s->zip || it->zip_entry < 0 || it->zip_entry >= s->zip->entry_count) return 0;
    if (!zip_read_locator_all(s->zip->path, &s->zip->entries[it->zip_entry].locator,
                              &data, &size)) return 0;
    return create_animation_bytes(data, size, thumbnail, now);
}

static void load_one_thumbnail(BrowserState* s, int first, int last) {
    int i;
    if (s->menu_open || (s->animate_previews && (s->touching || s->fling_velocity))) return;
    for (i = first; i <= last && i < s->item_count; i++) {
        BrowserItem* it = &s->items[i];
        if (!it->is_access && !it->is_dir && !it->is_system && it->thumb_state == 0) {
            uint32_t* thumb;
            PpmAnimation* animation;
            int static_ok;
            /* Failure to evict is intentional: it means all cached images are
               visible, so allocating this visible animation is preferable to
               flickering. The excess is bounded by the visible card count. */
            (void)evict_thumbnail(s, first, last);
            thumb = (uint32_t*)malloc(THUMB_PIXELS * sizeof(uint32_t));
            if (!thumb) { it->thumb_state = -1; return; }
            if (it->in_zip) {
                static_ok = decode_zip_thumbnail(s, it, thumb);
                animation = s->animate_previews && static_ok
                    ? create_zip_animation(s, it, thumb, monotonicish_millis()) : 0;
            } else {
                static_ok = decode_thumbnail(it->path, thumb);
                animation = s->animate_previews
                    ? create_animation(it->path, thumb, monotonicish_millis()) : 0;
            }
            if (animation || static_ok) {
                it->thumb = thumb;
                it->animation = animation;
                it->thumb_state = 1;
                it->stamp = ++s->stamp;
                s->thumb_count++;
            } else {
                free(thumb);
                it->thumb_state = -1;
            }
            s->redraw = 1;
            return;
        }
    }
}

static void animate_visible_thumbnails(BrowserState* s, int first, int last,
                                       uint32_t now) {
    int i;
    int changed = 0;
    if (!s->animate_previews || s->touching || s->fling_velocity || s->menu_open) return;
    for (i = first; i <= last && i < s->item_count; i++) {
        BrowserItem* it = &s->items[i];
        if (it->animation && it->thumb) {
            int result = advance_animation(it->animation, it->thumb, now);
            if (result < 0) {
                destroy_animation(it->animation);
                it->animation = 0;
            } else if (result > 0) {
                changed = 1;
                it->stamp = ++s->stamp;
            }
        }
    }
    if (changed) s->redraw = 1;
}

/* ---- Tiny bitmap font and drawing ------------------------------------- */
typedef struct Canvas {
    int width, height, stride, format;
    void* bits;
} Canvas;

static void put_pixel(Canvas* c, int x, int y, uint32_t rgb) {
    uint32_t r, g, b;
    if ((unsigned)x >= (unsigned)c->width || (unsigned)y >= (unsigned)c->height) return;
    r = (rgb >> 16) & 255u; g = (rgb >> 8) & 255u; b = rgb & 255u;
    if (c->format == 100) {
        ((uint32_t*)c->bits)[y * c->stride + x] = 0xff000000u | rgb;
    } else if (c->format == 4) {
        ((uint16_t*)c->bits)[y * c->stride + x] =
            (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    } else {
        ((uint32_t*)c->bits)[y * c->stride + x] =
            0xff000000u | r | (g << 8) | (b << 16);
    }
}

static void fill_rect(Canvas* c, int x, int y, int w, int h, uint32_t rgb) {
    int yy, xx;
    int x0 = imax(0, x), y0 = imax(0, y);
    int x1 = imin(c->width, x + w), y1 = imin(c->height, y + h);
    for (yy = y0; yy < y1; yy++) for (xx = x0; xx < x1; xx++) put_pixel(c, xx, yy, rgb);
}

static void stroke_rect(Canvas* c, int x, int y, int w, int h, int t, uint32_t rgb) {
    fill_rect(c, x, y, w, t, rgb); fill_rect(c, x, y + h - t, w, t, rgb);
    fill_rect(c, x, y, t, h, rgb); fill_rect(c, x + w - t, y, t, h, rgb);
}

/* Seven 5-bit rows per glyph. Lower-case characters are rendered uppercase. */
static uint8_t glyph_row(char input, int row) {
    static const uint8_t digits[10][7] = {
        {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
        {14,17,17,15,1,1,14}
    };
    static const uint8_t letters[26][7] = {
        {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{15,16,16,16,16,16,15},
        {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
        {15,16,16,19,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
        {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
        {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
        {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
    };
    char ch = input;
    if (row < 0 || row > 6) return 0;
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
    if (ch >= '0' && ch <= '9') return digits[ch - '0'][row];
    if (ch >= 'A' && ch <= 'Z') return letters[ch - 'A'][row];
    switch (ch) {
        case '.': return row == 6 ? 4 : 0;
        case ',': return row == 5 ? 4 : row == 6 ? 8 : 0;
        case ':': return row == 2 || row == 5 ? 4 : 0;
        case ';': return row == 2 || row == 5 ? 4 : row == 6 ? 8 : 0;
        case '-': return row == 3 ? 14 : 0;
        case '_': return row == 6 ? 31 : 0;
        /* Bit 4 is the leftmost column: a slash runs from top right to
           bottom left, a backslash the other way (swapped before 1.4.14). */
        case '/': return (uint8_t)(1 << imin(4, imax(0, row - 1)));
        case '\\': return (uint8_t)(1 << (4 - imin(4, imax(0, row - 1))));
        case '[': return row == 0 || row == 6 ? 14 : 8;
        case ']': return row == 0 || row == 6 ? 14 : 2;
        case '(': return row == 0 || row == 6 ? 2 : 4;
        case ')': return row == 0 || row == 6 ? 8 : 4;
        case '+': return row == 3 ? 14 : row == 2 || row == 4 ? 4 : 0;
        case '=': return row == 2 || row == 4 ? 14 : 0;
        case '%': return row == 0 || row == 1 ? 17 : row == 5 || row == 6 ? 17 : row == 2 ? 2 : row == 4 ? 8 : 4;
        case '?': return row == 0 ? 14 : row == 1 ? 17 : row == 2 ? 2 : row == 3 ? 4 : row == 5 ? 4 : 0;
        case '!': return row < 5 ? 4 : row == 6 ? 4 : 0;
        case '#': return row == 2 || row == 4 ? 31 : 10;
        case '<': return row == 2 ? 2 : row == 3 ? 4 : row == 4 ? 2 : 0;
        case '>': return row == 2 ? 8 : row == 3 ? 4 : row == 4 ? 8 : 0;
        case ' ': return 0;
        default: return row == 0 || row == 6 ? 14 : row == 1 || row == 5 ? 17 : row == 3 ? 2 : 0;
    }
}

static void draw_char(Canvas* c, int x, int y, char ch, int scale, uint32_t rgb) {
    int row, col;
    for (row = 0; row < 7; row++) {
        uint8_t bits = glyph_row(ch, row);
        for (col = 0; col < 5; col++) if (bits & (1 << (4 - col)))
            fill_rect(c, x + col * scale, y + row * scale, scale, scale, rgb);
    }
}

static int draw_text(Canvas* c, int x, int y, const char* text, int scale,
                     uint32_t rgb, int max_width) {
    int start = x, advance = 6 * scale;
    while (*text && x + 5 * scale <= start + max_width) {
        unsigned char ch = (unsigned char)*text++;
        if (ch >= 128) ch = '?';
        draw_char(c, x, y, (char)ch, scale, rgb);
        x += advance;
    }
    return x;
}

static void draw_folder(Canvas* c, int x, int y, int w, int h, int parent) {
    int fw = imin(w - 16, 92), fh = imin(h - 18, 62);
    int fx = x + (w - fw) / 2, fy = y + (h - fh) / 2 + 4;
    uint32_t col = parent ? 0x7eb6ff : 0xf6bd3b;
    fill_rect(c, fx + 4, fy, fw / 2, imax(5, fh / 6), col);
    fill_rect(c, fx, fy + imax(4, fh / 8), fw, fh - imax(4, fh / 8), col);
    stroke_rect(c, fx, fy + imax(4, fh / 8), fw, fh - imax(4, fh / 8), 2, 0x20242b);
    if (parent) draw_text(c, fx + fw / 2 - 8, fy + fh / 2 - 5, "..", 1, 0x111318, 20);
}

/* A ZIP archive card: folder silhouette in its own colour plus a zipper
   line and "ZIP" label, drawn from rectangles like the other icons. */
static void draw_archive(Canvas* c, int x, int y, int w, int h) {
    int fw = imin(w - 16, 92), fh = imin(h - 18, 62);
    int fx = x + (w - fw) / 2, fy = y + (h - fh) / 2 + 4;
    int tab = imax(4, fh / 8), i;
    uint32_t col = 0x4fc3a1;
    fill_rect(c, fx + 4, fy, fw / 2, imax(5, fh / 6), col);
    fill_rect(c, fx, fy + tab, fw, fh - tab, col);
    stroke_rect(c, fx, fy + tab, fw, fh - tab, 2, 0x20242b);
    for (i = fy + tab + 6; i < fy + fh - 6; i += 6)
        fill_rect(c, fx + fw / 2 - 2, i, 4, 3, 0x20242b);
    draw_text(c, fx + 6, fy + fh - 13, "ZIP", 1, 0x111318, fw - 12);
}

static void draw_system_picker(Canvas* c, int x, int y, int w, int h) {
    int fw = imin(w - 16, 92), fh = imin(h - 18, 62);
    int fx = x + (w - fw) / 2, fy = y + (h - fh) / 2 + 4;
    fill_rect(c, fx, fy, fw, fh, 0x78a9e6);
    stroke_rect(c, fx, fy, fw, fh, 2, 0x20242b);
    fill_rect(c, fx + 8, fy + 10, fw - 16, fh - 20, 0xe8f2ff);
    draw_text(c, fx + fw / 2 - 14, fy + fh / 2 - 4, "...", 1, 0x203247, 28);
}

static void draw_access(Canvas* c, int x, int y, int w, int h) {
    int bw = imin(w - 22, 76), bh = imin(h - 24, 54);
    int bx = x + (w - bw) / 2, by = y + (h - bh) / 2 + 7;
    int shackle_w = bw / 2, shackle_h = imax(12, bh / 3);
    int sx = bx + (bw - shackle_w) / 2;
    /* Simple padlock icon, intentionally built from rectangles so it works
       with the tiny renderer on API 9 and without extra bitmap resources. */
    stroke_rect(c, sx, by - shackle_h + 5, shackle_w, shackle_h + 2, 4, 0xf2c15b);
    fill_rect(c, bx, by + 2, bw, bh - 2, 0xf2c15b);
    stroke_rect(c, bx, by + 2, bw, bh - 2, 2, 0x20242b);
    fill_rect(c, bx + bw / 2 - 3, by + bh / 2 - 2, 6, 15, 0x20242b);
    fill_rect(c, bx + bw / 2 - 6, by + bh / 2 - 7, 12, 12, 0x20242b);
}

static void draw_thumb_scaled(Canvas* c, const uint32_t* thumb, int x, int y, int w, int h) {
    int yy, xx;
    int draw_w = w;
    int draw_h = (w * THUMB_H) / THUMB_W;
    int draw_x, draw_y;

    /* Keep the native 4:3 Flipnote aspect ratio. This is also a safety net
       for unusual displays in case the surrounding card size is rounded. */
    if (draw_h > h) {
        draw_h = h;
        draw_w = (h * THUMB_W) / THUMB_H;
    }
    draw_w = imax(1, draw_w);
    draw_h = imax(1, draw_h);
    draw_x = x + (w - draw_w) / 2;
    draw_y = y + (h - draw_h) / 2;

    fill_rect(c, x, y, w, h, 0x111318);
    for (yy = 0; yy < draw_h; yy++) {
        int sy = (yy * THUMB_H) / draw_h;
        for (xx = 0; xx < draw_w; xx++) {
            int sx = (xx * THUMB_W) / draw_w;
            put_pixel(c, draw_x + xx, draw_y + yy,
                      thumb[sy * THUMB_W + sx]);
        }
    }
}

static void draw_placeholder(Canvas* c, int x, int y, int w, int h, int invalid) {
    int yy, xx, block = imax(5, w / 10);
    uint32_t a = invalid ? 0x512d35 : 0x272b33;
    uint32_t b = invalid ? 0x7a3d46 : 0x30353f;
    for (yy = 0; yy < h; yy += block) for (xx = 0; xx < w; xx += block)
        fill_rect(c, x + xx, y + yy, imin(block, w - xx), imin(block, h - yy),
                  (((xx / block) + (yy / block)) & 1) ? a : b);
    if (invalid) draw_text(c, x + w / 2 - 15, y + h / 2 - 4, "PPM?", 1, 0xffffff, 30);
}

static void size_text(int bytes, char* out) {
    int n, p = 0;
    char rev[12];
    const char* suffix;
    if (bytes < 0) { copy_text(out, "? KB", 16); return; }
    if (bytes >= 1024 * 1024) { n = bytes / (1024 * 1024); suffix = " MB"; }
    else { n = imax(1, bytes / 1024); suffix = " KB"; }
    if (n == 0) rev[p++] = '0';
    while (n > 0 && p < 10) { rev[p++] = (char)('0' + (n % 10)); n /= 10; }
    n = 0;
    while (p > 0) out[n++] = rev[--p];
    p = 0;
    while (suffix[p] && n < 15) out[n++] = suffix[p++];
    out[n] = 0;
}

static void layout_values(int width, int height, int* cols, int* margin,
                          int* gap, int* header, int* footer, int* card_w,
                          int* thumb_h, int* card_h) {
    (void)height;
    /* The old layout stopped at four columns and capped thumbnail height.
       On wide landscape displays this made every 4:3 Flipnote extremely
       wide. Keep the legacy layout on small screens, but add columns on
       wide screens so the cards remain close to their natural size. */
    if (width >= 900) {
        *cols = width / 210;
        *cols = iclamp(*cols, 4, 10);
    } else {
        *cols = width >= 720 ? 4 : width >= 470 ? 3 : 2;
    }
    *margin = width < 360 ? 4 : 6;
    *gap = width < 360 ? 4 : 6;
    *header = width < 360 ? 40 : 46;
    *footer = 22;
    *card_w = (width - 2 * (*margin) - (*gap) * ((*cols) - 1)) / (*cols);
    /* The actual image area is always 4:3. No fixed height cap: the number
       of columns above controls the visual size on large displays. */
    *thumb_h = imax(1, ((*card_w - 8) * THUMB_H) / THUMB_W);
    *card_h = *thumb_h + 31;
}

static int max_scroll_for(BrowserState* s, int w, int h) {
    int cols, margin, gap, header, footer, card_w, thumb_h, card_h;
    int rows, content;
    layout_values(w, h, &cols, &margin, &gap, &header, &footer, &card_w, &thumb_h, &card_h);
    rows = (s->item_count + cols - 1) / cols;
    content = rows * (card_h + gap);
    return imax(0, content - (h - header - footer - margin));
}

static void visible_range(BrowserState* s, int w, int h, int* first, int* last) {
    int cols, margin, gap, header, footer, card_w, thumb_h, card_h;
    int first_row, last_row;
    layout_values(w, h, &cols, &margin, &gap, &header, &footer, &card_w, &thumb_h, &card_h);
    first_row = s->scroll / (card_h + gap);
    last_row = (s->scroll + (h - header - footer) + card_h + gap - 1) / (card_h + gap);
    *first = first_row * cols;
    *last = imin(s->item_count - 1, (last_row + 1) * cols - 1);
}

#include "browser_controls.inc"

static void draw_browser(BrowserState* s, Canvas* c) {
    int cols, margin, gap, header, footer, card_w, thumb_h, card_h;
    int i, first, last;
    char sizebuf[16];
    fill_rect(c, 0, 0, c->width, c->height, 0x111318);
    layout_values(c->width, c->height, &cols, &margin, &gap, &header, &footer,
                  &card_w, &thumb_h, &card_h);
    s->scroll = iclamp(s->scroll, 0, max_scroll_for(s, c->width, c->height));

    fill_rect(c, 0, 0, c->width, header, 0x1c2027);
    draw_text(c, margin, 5, "FLIPNOTE-BROWSER", c->width >= 470 ? 2 : 1,
              0xffffff, c->width - 2 * margin);
    draw_text(c, margin, header - 13, s->directory, 1, 0x9cb3d0, c->width - 2 * margin);

    visible_range(s, c->width, c->height, &first, &last);
    for (i = first; i <= last && i < s->item_count; i++) {
        BrowserItem* it = &s->items[i];
        int row = i / cols, col = i % cols;
        int x = margin + col * (card_w + gap);
        int y = header + margin + row * (card_h + gap) - s->scroll;
        int tx = x + 4, ty = y + 4, tw = card_w - 8;
        uint32_t card = i == s->selected ? 0x34445a : 0x20242b;
        fill_rect(c, x, y, card_w, card_h, card);
        stroke_rect(c, x, y, card_w, card_h, i == s->selected ? 2 : 1,
                    i == s->selected ? 0x8fc7ff : 0x363c47);
        if (it->is_access) {
            draw_access(c, tx, ty, tw, thumb_h);
        } else if (it->is_system) {
            draw_system_picker(c, tx, ty, tw, thumb_h);
        } else if (it->is_archive) {
            draw_archive(c, tx, ty, tw, thumb_h);
        } else if (it->is_dir) {
            draw_folder(c, tx, ty, tw, thumb_h, it->is_parent);
        } else if (it->thumb) {
            draw_thumb_scaled(c, it->thumb, tx, ty, tw, thumb_h);
            it->stamp = ++s->stamp;
        } else {
            draw_placeholder(c, tx, ty, tw, thumb_h, it->thumb_state < 0);
        }
        draw_text(c, x + 4, y + thumb_h + 8, it->name, 1, 0xffffff, card_w - 8);
        if (it->is_access) {
            draw_text(c, x + 4, y + thumb_h + 19,
                      "ANDROID-EINSTELLUNG", 1, 0x9ca5b3, card_w - 8);
        } else if (it->is_system) {
            draw_text(c, x + 4, y + thumb_h + 19,
                      "ANDROID-DIALOG", 1, 0x9ca5b3, card_w - 8);
        } else if (it->is_archive) {
            draw_text(c, x + 4, y + thumb_h + 19, "ZIP-ARCHIV", 1, 0x9ca5b3, card_w - 8);
        } else if (!it->is_dir) {
            if (it->in_zip && s->zip && it->zip_entry >= 0
                    && it->zip_entry < s->zip->entry_count
                    && !zip_locator_supported(&s->zip->entries[it->zip_entry].locator)) {
                draw_text(c, x + 4, y + thumb_h + 19, "NICHT UNTERSTUETZT", 1,
                          0xffc26b, card_w - 8);
            } else {
                size_text(it->size, sizebuf);
                draw_text(c, x + 4, y + thumb_h + 19, sizebuf, 1, 0x9ca5b3, card_w - 8);
            }
        } else {
            draw_text(c, x + 4, y + thumb_h + 19,
                      it->is_parent ? "ORDNER HOCH" : "ORDNER", 1, 0x9ca5b3, card_w - 8);
        }
    }

    fill_rect(c, 0, c->height - footer, c->width, footer, 0x1c2027);
    if (s->message[0]) {
        draw_text(c, margin, c->height - footer + 7, s->message, 1, 0xffc26b,
                  c->width - 2 * margin);
    } else {
        draw_text(c, margin, c->height - footer + 7,
                  s->animate_previews ? "TIPPEN: OEFFNEN   VORSCHAU: ANIMIERT"
                                      : "TIPPEN: OEFFNEN   VORSCHAU: STATISCH", 1, 0xb7c0cc,
                  c->width - 2 * margin);
    }
    browser_draw_header_and_menu(s, c, header, margin);
}

/* ---- Activity result and permission helpers --------------------------- */
static void jni_clear(JNIEnv* env) {
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
}

#include "gallery_pager.inc"
#include "browser_preferences.inc"

static int request_storage_permission(ANativeActivity* a) {
    JNIEnv* env = a->env;
    jclass activity_cls = 0, string_cls = 0;
    jmethodID check, request;
    jstring permission = 0;
    jobjectArray permissions = 0;
    jint result = -1;
    int granted = 0;
    if (a->sdkVersion < 23 || a->sdkVersion >= 33) return 1;
    activity_cls = (*env)->GetObjectClass(env, a->clazz);
    if (!activity_cls) { jni_clear(env); return 1; }
    check = (*env)->GetMethodID(env, activity_cls, "checkSelfPermission", "(Ljava/lang/String;)I");
    if (!check) { jni_clear(env); granted = 1; goto done; }
    permission = (*env)->NewStringUTF(env, "android.permission.READ_EXTERNAL_STORAGE");
    if (!permission) { jni_clear(env); granted = 1; goto done; }
    result = (*env)->CallIntMethod(env, a->clazz, check, permission);
    if ((*env)->ExceptionCheck(env)) { jni_clear(env); granted = 1; goto done; }
    if (result == 0) { granted = 1; goto done; }
    request = (*env)->GetMethodID(env, activity_cls, "requestPermissions", "([Ljava/lang/String;I)V");
    if (!request) { jni_clear(env); goto done; }
    string_cls = (*env)->FindClass(env, "java/lang/String");
    if (!string_cls) { jni_clear(env); goto done; }
    permissions = (*env)->NewObjectArray(env, 1, string_cls, permission);
    if (!permissions) { jni_clear(env); goto done; }
    (*env)->CallVoidMethod(env, a->clazz, request, permissions, 6123);
    jni_clear(env);
done:
    if (permissions) (*env)->DeleteLocalRef(env, permissions);
    if (string_cls) (*env)->DeleteLocalRef(env, string_cls);
    if (permission) (*env)->DeleteLocalRef(env, permission);
    if (activity_cls) (*env)->DeleteLocalRef(env, activity_cls);
    return granted;
}

static int permission_granted_worker(BrowserState* s, JNIEnv* env) {
    jclass cls;
    jmethodID check;
    jstring permission;
    jint result = -1;
    if (s->activity->sdkVersion < 23 || s->activity->sdkVersion >= 33) return 1;
    cls = (*env)->GetObjectClass(env, s->activity->clazz);
    if (!cls) { jni_clear(env); return 0; }
    check = (*env)->GetMethodID(env, cls, "checkSelfPermission", "(Ljava/lang/String;)I");
    if (!check) {
        jni_clear(env);
        (*env)->DeleteLocalRef(env, cls);
        return 1;
    }
    permission = (*env)->NewStringUTF(env, "android.permission.READ_EXTERNAL_STORAGE");
    if (permission)
        result = (*env)->CallIntMethod(env, s->activity->clazz, check, permission);
    if ((*env)->ExceptionCheck(env)) { jni_clear(env); result = -1; }
    if (permission) (*env)->DeleteLocalRef(env, permission);
    (*env)->DeleteLocalRef(env, cls);
    return result == 0;
}

static int has_manage_storage_access(BrowserState* s, JNIEnv* env) {
    jclass environment_cls;
    jmethodID is_manager;
    jboolean granted = 0;
    if (s->activity->sdkVersion < 33) return 1;
    environment_cls = (*env)->FindClass(env, "android/os/Environment");
    if (!environment_cls) { jni_clear(env); return 0; }
    is_manager = (*env)->GetStaticMethodID(env, environment_cls,
                                           "isExternalStorageManager", "()Z");
    if (!is_manager) {
        jni_clear(env);
        (*env)->DeleteLocalRef(env, environment_cls);
        return 0;
    }
    granted = (*env)->CallStaticBooleanMethod(env, environment_cls, is_manager);
    if ((*env)->ExceptionCheck(env)) { jni_clear(env); granted = 0; }
    (*env)->DeleteLocalRef(env, environment_cls);
    return granted ? 1 : 0;
}

static void launch_manage_storage_settings(BrowserState* s, JNIEnv* env) {
    jclass intent_cls = 0, activity_cls = 0, uri_cls = 0;
    jmethodID intent_ctor = 0, start = 0, get_package = 0, from_parts = 0;
    jstring action = 0, scheme = 0, package_name = 0;
    jobject package_uri = 0, intent = 0;
    int launched = 0;
    if (s->activity->sdkVersion < 30) return;

    intent_cls = (*env)->FindClass(env, "android/content/Intent");
    uri_cls = (*env)->FindClass(env, "android/net/Uri");
    activity_cls = (*env)->GetObjectClass(env, s->activity->clazz);
    if (!intent_cls || !uri_cls || !activity_cls) {
        jni_clear(env);
        goto done;
    }
    intent_ctor = (*env)->GetMethodID(env, intent_cls, "<init>",
                                      "(Ljava/lang/String;Landroid/net/Uri;)V");
    start = (*env)->GetMethodID(env, activity_cls, "startActivity",
                                "(Landroid/content/Intent;)V");
    get_package = (*env)->GetMethodID(env, activity_cls, "getPackageName",
                                      "()Ljava/lang/String;");
    from_parts = (*env)->GetStaticMethodID(env, uri_cls, "fromParts",
                "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Landroid/net/Uri;");
    if (!intent_ctor || !start || !get_package || !from_parts) {
        jni_clear(env);
        goto fallback;
    }

    package_name = (jstring)(*env)->CallObjectMethod(env, s->activity->clazz, get_package);
    scheme = (*env)->NewStringUTF(env, "package");
    action = (*env)->NewStringUTF(env,
             "android.settings.MANAGE_APP_ALL_FILES_ACCESS_PERMISSION");
    if (!package_name || !scheme || !action) { jni_clear(env); goto fallback; }
    package_uri = (*env)->CallStaticObjectMethod(env, uri_cls, from_parts,
                                                 scheme, package_name, 0);
    intent = (*env)->NewObject(env, intent_cls, intent_ctor, action, package_uri);
    if (intent) {
        (*env)->CallVoidMethod(env, s->activity->clazz, start, intent);
        if (!(*env)->ExceptionCheck(env)) launched = 1;
        else jni_clear(env);
    }

fallback:
    if (!launched) {
        jmethodID simple_ctor;
        jstring generic_action;
        jobject generic_intent;
        simple_ctor = intent_cls ? (*env)->GetMethodID(env, intent_cls, "<init>",
                                             "(Ljava/lang/String;)V") : 0;
        generic_action = (*env)->NewStringUTF(env,
                         "android.settings.MANAGE_ALL_FILES_ACCESS_PERMISSION");
        generic_intent = (simple_ctor && generic_action)
            ? (*env)->NewObject(env, intent_cls, simple_ctor, generic_action) : 0;
        if (generic_intent && start) {
            (*env)->CallVoidMethod(env, s->activity->clazz, start, generic_intent);
            if (!(*env)->ExceptionCheck(env)) launched = 1;
            else jni_clear(env);
        }
        if (generic_intent) (*env)->DeleteLocalRef(env, generic_intent);
        if (generic_action) (*env)->DeleteLocalRef(env, generic_action);
    }

done:
    if (launched) {
        s->manage_settings_open = 1;
        set_message(s, "ZUGRIFF AKTIVIEREN UND DANN ZUR APP ZURUECK");
    } else {
        set_message(s, "ZUGRIFFSEINSTELLUNG NICHT VERFUEGBAR");
    }
    s->redraw = 1;
    if (intent) (*env)->DeleteLocalRef(env, intent);
    if (package_uri) (*env)->DeleteLocalRef(env, package_uri);
    if (action) (*env)->DeleteLocalRef(env, action);
    if (scheme) (*env)->DeleteLocalRef(env, scheme);
    if (package_name) (*env)->DeleteLocalRef(env, package_name);
    if (activity_cls) (*env)->DeleteLocalRef(env, activity_cls);
    if (uri_cls) (*env)->DeleteLocalRef(env, uri_cls);
    if (intent_cls) (*env)->DeleteLocalRef(env, intent_cls);
}

static void relax_file_uri_policy(JNIEnv* env) {
    jclass builder_cls = (*env)->FindClass(env, "android/os/StrictMode$VmPolicy$Builder");
    jclass strict_cls;
    jmethodID ctor, build, set_policy;
    jobject builder, policy;
    if (!builder_cls) { jni_clear(env); return; }
    ctor = (*env)->GetMethodID(env, builder_cls, "<init>", "()V");
    build = (*env)->GetMethodID(env, builder_cls, "build", "()Landroid/os/StrictMode$VmPolicy;");
    if (!ctor || !build) { jni_clear(env); return; }
    builder = (*env)->NewObject(env, builder_cls, ctor);
    policy = (*env)->CallObjectMethod(env, builder, build);
    strict_cls = (*env)->FindClass(env, "android/os/StrictMode");
    set_policy = strict_cls ? (*env)->GetStaticMethodID(env, strict_cls, "setVmPolicy",
                    "(Landroid/os/StrictMode$VmPolicy;)V") : 0;
    if (set_policy && policy) (*env)->CallStaticVoidMethod(env, strict_cls, set_policy, policy);
    jni_clear(env);
}

static void launch_system_picker(BrowserState* s, JNIEnv* env) {
    jclass intent_cls = 0, activity_cls = 0;
    jmethodID ctor, add_category, set_type, add_flags, start;
    jstring action = 0, category = 0, type = 0;
    jobject intent = 0;
    if (s->finishing) return;
    intent_cls = (*env)->FindClass(env, "android/content/Intent");
    if (!intent_cls) { jni_clear(env); return; }
    ctor = (*env)->GetMethodID(env, intent_cls, "<init>", "(Ljava/lang/String;)V");
    add_category = (*env)->GetMethodID(env, intent_cls, "addCategory",
                                      "(Ljava/lang/String;)Landroid/content/Intent;");
    set_type = (*env)->GetMethodID(env, intent_cls, "setType",
                                  "(Ljava/lang/String;)Landroid/content/Intent;");
    add_flags = (*env)->GetMethodID(env, intent_cls, "addFlags",
                                   "(I)Landroid/content/Intent;");
    activity_cls = (*env)->GetObjectClass(env, s->activity->clazz);
    start = activity_cls ? (*env)->GetMethodID(env, activity_cls, "startActivity",
                                               "(Landroid/content/Intent;)V") : 0;
    if (!ctor || !add_category || !set_type || !add_flags || !start) {
        jni_clear(env);
        set_message(s, "SYSTEM-DATEIAUSWAHL NICHT VERFUEGBAR");
        s->redraw = 1;
        return;
    }
    action = (*env)->NewStringUTF(env, "android.intent.action.OPEN_DOCUMENT");
    category = (*env)->NewStringUTF(env, "android.intent.category.OPENABLE");
    type = (*env)->NewStringUTF(env, "*/*");
    intent = (*env)->NewObject(env, intent_cls, ctor, action);
    if (intent) {
        (*env)->CallObjectMethod(env, intent, add_category, category);
        (*env)->CallObjectMethod(env, intent, set_type, type);
        /* FLAG_ACTIVITY_FORWARD_RESULT | FLAG_GRANT_READ_URI_PERMISSION */
        (*env)->CallObjectMethod(env, intent, add_flags, 0x02000001);
        (*env)->CallVoidMethod(env, s->activity->clazz, start, intent);
    }
    if ((*env)->ExceptionCheck(env)) {
        jni_clear(env);
        set_message(s, "SYSTEM-DATEIAUSWAHL NICHT VERFUEGBAR");
        s->redraw = 1;
    } else {
        s->finishing = 1;
        ANativeActivity_finish(s->activity);
        s->running = 0;
    }
    if (intent) (*env)->DeleteLocalRef(env, intent);
    if (type) (*env)->DeleteLocalRef(env, type);
    if (category) (*env)->DeleteLocalRef(env, category);
    if (action) (*env)->DeleteLocalRef(env, action);
    if (activity_cls) (*env)->DeleteLocalRef(env, activity_cls);
    (*env)->DeleteLocalRef(env, intent_cls);
}

static void return_selected_file(BrowserState* s, JNIEnv* env, const char* path) {
    jclass file_cls, uri_cls, intent_cls, activity_cls;
    jmethodID file_ctor, from_file, intent_ctor, set_data, add_flags, set_result;
    jstring jpath;
    jobject file_obj, uri_obj, intent_obj;
    if (s->finishing || !env) return;
    s->finishing = 1;
    relax_file_uri_policy(env);
    file_cls = (*env)->FindClass(env, "java/io/File");
    uri_cls = (*env)->FindClass(env, "android/net/Uri");
    intent_cls = (*env)->FindClass(env, "android/content/Intent");
    if (!file_cls || !uri_cls || !intent_cls) { jni_clear(env); s->finishing = 0; return; }
    file_ctor = (*env)->GetMethodID(env, file_cls, "<init>", "(Ljava/lang/String;)V");
    from_file = (*env)->GetStaticMethodID(env, uri_cls, "fromFile", "(Ljava/io/File;)Landroid/net/Uri;");
    intent_ctor = (*env)->GetMethodID(env, intent_cls, "<init>", "()V");
    set_data = (*env)->GetMethodID(env, intent_cls, "setData", "(Landroid/net/Uri;)Landroid/content/Intent;");
    add_flags = (*env)->GetMethodID(env, intent_cls, "addFlags", "(I)Landroid/content/Intent;");
    activity_cls = (*env)->GetObjectClass(env, s->activity->clazz);
    set_result = (*env)->GetMethodID(env, activity_cls, "setResult", "(ILandroid/content/Intent;)V");
    if (!file_ctor || !from_file || !intent_ctor || !set_data || !set_result) {
        jni_clear(env); s->finishing = 0; return;
    }
    jpath = (*env)->NewStringUTF(env, path);
    file_obj = (*env)->NewObject(env, file_cls, file_ctor, jpath);
    uri_obj = (*env)->CallStaticObjectMethod(env, uri_cls, from_file, file_obj);
    intent_obj = (*env)->NewObject(env, intent_cls, intent_ctor);
    (*env)->CallObjectMethod(env, intent_obj, set_data, uri_obj);
    if (add_flags) (*env)->CallObjectMethod(env, intent_obj, add_flags, 1);
    (*env)->CallVoidMethod(env, s->activity->clazz, set_result, -1, intent_obj);
    if ((*env)->ExceptionCheck(env)) {
        jni_clear(env);
        s->finishing = 0;
        set_message(s, "DATEI KONNTE NICHT UEBERGEBEN WERDEN");
        s->redraw = 1;
        return;
    }
    ANativeActivity_finish(s->activity);
    s->running = 0;
}

/* ---- Interaction ------------------------------------------------------- */
static void open_item(BrowserState* s, JNIEnv* env, int index) {
    BrowserItem* it;
    char cache_path[MAX_PATH];
    if (index < 0 || index >= s->item_count) return;
    it = &s->items[index];
    if (it->is_access) launch_manage_storage_settings(s, env);
    else if (it->is_system) launch_system_picker(s, env);
    else if (it->is_dir) scan_directory(s, it->path);
    else if (it->in_zip) {
        /* The Java player needs an ordinary file: extract into the app
           cache first, then hand that copy over exactly like a file. */
        const ZipLocator* loc = browser_item_locator(s, it);
        if (!loc || !zip_locator_supported(loc)) {
            set_message(s, "ZIP-EINTRAG NICHT UNTERSTUETZT (VERSCHLUESSELT/METHODE)");
        } else if (!browser_item_cache_path(s, it, cache_path)) {
            set_message(s, "KEIN CACHE-ORDNER FUER ZIP-DATEIEN");
        } else if (!zip_cache_ensure(cache_path, s->zip->path, loc)) {
            set_message(s, "ENTPACKEN FEHLGESCHLAGEN - ZIP DEFEKT ODER SPEICHER VOLL");
        } else {
            gallery_prepare_selection(s, index);
            return_selected_file(s, env, cache_path);
            return;
        }
        s->redraw = 1;
    } else {
        gallery_prepare_selection(s, index);
        return_selected_file(s, env, it->path);
    }
}

static void go_parent_or_cancel(BrowserState* s) {
    char parent[MAX_PATH];
    if (s->permission_pending || s->directory[0] != '/') {
        ANativeActivity_finish(s->activity);
        s->running = 0;
    } else if (!path_is_root(s->directory)) {
        parent_path(s->directory, parent);
        scan_directory(s, parent);
    } else {
        ANativeActivity_finish(s->activity);
        s->running = 0;
    }
}

static int item_at(BrowserState* s, int w, int h, int x, int y) {
    int cols, margin, gap, header, footer, card_w, thumb_h, card_h;
    int cy, row, col, localx, idx;
    layout_values(w, h, &cols, &margin, &gap, &header, &footer, &card_w, &thumb_h, &card_h);
    if (y < header || y >= h - footer || x < margin) return -1;
    cy = y - header - margin + s->scroll;
    if (cy < 0) return -1;
    row = cy / (card_h + gap);
    if ((cy % (card_h + gap)) >= card_h) return -1;
    localx = x - margin;
    col = localx / (card_w + gap);
    if (col < 0 || col >= cols || (localx % (card_w + gap)) >= card_w) return -1;
    idx = row * cols + col;
    return idx < s->item_count ? idx : -1;
}

static void ensure_selection_visible(BrowserState* s, int w, int h) {
    int cols, margin, gap, header, footer, card_w, thumb_h, card_h;
    int row, top, bottom, viewport;
    layout_values(w, h, &cols, &margin, &gap, &header, &footer, &card_w, &thumb_h, &card_h);
    row = s->selected / cols;
    top = row * (card_h + gap);
    bottom = top + card_h;
    viewport = h - header - footer - margin;
    if (top < s->scroll) s->scroll = top;
    else if (bottom > s->scroll + viewport) s->scroll = bottom - viewport;
    s->scroll = iclamp(s->scroll, 0, max_scroll_for(s, w, h));
}

static void handle_key(BrowserState* s, JNIEnv* env, int key, int w, int h) {
    s->fling_velocity = 0;
    if (key == 82) { s->menu_open = !s->menu_open; s->redraw = 1; return; }
    if (s->menu_open) {
        if (key == AKEYCODE_BACK) s->menu_open = 0;
        else if (key == AKEYCODE_DPAD_CENTER || key == AKEYCODE_ENTER) {
            browser_set_animation(s, !s->animate_previews);
            browser_save_preferences(s, env);
        }
        s->redraw = 1;
        return;
    }

    int cols, margin, gap, header, footer, card_w, thumb_h, card_h;
    if (key == AKEYCODE_BACK) { go_parent_or_cancel(s); return; }
    if (s->item_count <= 0) return;
    layout_values(w, h, &cols, &margin, &gap, &header, &footer, &card_w, &thumb_h, &card_h);
    if (key == AKEYCODE_DPAD_LEFT) s->selected--;
    else if (key == AKEYCODE_DPAD_RIGHT) s->selected++;
    else if (key == AKEYCODE_DPAD_UP) s->selected -= cols;
    else if (key == AKEYCODE_DPAD_DOWN) s->selected += cols;
    else if (key == AKEYCODE_DPAD_CENTER || key == AKEYCODE_ENTER) {
        open_item(s, env, s->selected); return;
    } else return;
    s->selected = iclamp(s->selected, 0, s->item_count - 1);
    ensure_selection_visible(s, w, h);
    s->redraw = 1;
}

#include "browser_gestures.inc"

static void handle_motion(BrowserState* s, JNIEnv* env, const AInputEvent* event,
                          int w, int h) {
    int action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
    int x = float_bits_to_int(AMotionEvent_getX(event, 0));
    int y = float_bits_to_int(AMotionEvent_getY(event, 0));
    browser_handle_motion(s, env, action, x, y, w, h,
                          browser_event_ms(AMotionEvent_getEventTime(event)));
}

/* ---- Worker and callbacks --------------------------------------------- */
static void choose_start_directory(BrowserState* s) {
    char path[MAX_PATH];
    const char* ext = s->activity->externalDataPath;
    int i;
    path[0] = 0;

    /* A return from the player should reopen the exact originating folder
       and highlight the Flipnote that is currently active after swiping.
       Both paths may point into a ZIP archive. */
    if (s->restore_pending) {
        int found = -1;
        s->restore_pending = 0;
        if (browser_target_readable(s->restore_directory)
                && scan_directory(s, s->restore_directory)) {
            for (i = 0; i < s->item_count; i++) {
                if (!s->items[i].is_access && !s->items[i].is_system
                        && !s->items[i].is_dir
                        && strcmp(s->items[i].path, s->restore_path) == 0) {
                    found = i;
                    break;
                }
            }
            if (found >= 0) {
                s->selected = found;
                s->reveal_selected = 1;
            }
            return;
        }
    }
    /* Otherwise continue where the browser was last closed. A folder that
       has disappeared (SD card removed, archive deleted) falls through to
       the storage roots below. */
    if (browser_target_readable(s->last_directory)
            && scan_directory(s, s->last_directory)) return;
    if (ext && ext[0]) {
        copy_text(path, ext, MAX_PATH);
        for (i = 0; path[i]; i++) {
            const char marker[] = "/Android/data/";
            int k = 0;
            while (marker[k] && path[i + k] == marker[k]) k++;
            if (!marker[k]) { path[i] = 0; break; }
        }
        if (directory_readable(path)) { scan_directory(s, path); return; }
    }
    if (directory_readable("/sdcard")) { scan_directory(s, "/sdcard"); return; }
    if (directory_readable("/mnt/sdcard")) { scan_directory(s, "/mnt/sdcard"); return; }
    if (directory_readable("/storage/emulated/0")) { scan_directory(s, "/storage/emulated/0"); return; }
    if (ext && directory_readable(ext)) { scan_directory(s, ext); return; }
    scan_directory(s, "/");
}

static void render_if_needed(BrowserState* s, int* out_w, int* out_h) {
    ANativeWindow* window;
    ANativeWindow_Buffer buffer;
    Canvas canvas;
    if (!s->redraw) return;
    if (try_lock(&s->draw_busy)) return;
    window = (ANativeWindow*)s->window;
    if (!window) { release_lock(&s->draw_busy); return; }
    if (ANativeWindow_lock(window, &buffer, 0) == 0) {
        canvas.width = buffer.width;
        canvas.height = buffer.height;
        canvas.stride = buffer.stride;
        canvas.format = buffer.format;
        canvas.bits = buffer.bits;
        if (s->reveal_selected) {
            ensure_selection_visible(s, buffer.width, buffer.height);
            s->reveal_selected = 0;
        }
        draw_browser(s, &canvas);
        *out_w = buffer.width; *out_h = buffer.height;
        ANativeWindow_unlockAndPost(window);
        s->redraw = 0;
    }
    release_lock(&s->draw_busy);
}

static void process_input(BrowserState* s, JNIEnv* env, int w, int h) {
    AInputQueue* queue;
    AInputEvent* event;
    int handled;
    if (try_lock(&s->input_busy)) return;
    queue = (AInputQueue*)s->input;
    if (!queue) { release_lock(&s->input_busy); return; }
    while (AInputQueue_getEvent(queue, &event) >= 0) {
        handled = 1;
        if (AInputQueue_preDispatchEvent(queue, event)) continue;
        if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
            if (AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_DOWN)
                handle_key(s, env, AKeyEvent_getKeyCode(event), w, h);
        } else if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
            handle_motion(s, env, event, w, h);
        } else handled = 0;
        AInputQueue_finishEvent(queue, event, handled);
    }
    release_lock(&s->input_busy);
}

static void* browser_thread(void* arg) {
    BrowserState* s = (BrowserState*)arg;
    JNIEnv* env = 0;
    int w = 320, h = 440;
    int first = 0, last = -1;
    (*s->activity->vm)->AttachCurrentThread(s->activity->vm, (void**)&env, 0);
    browser_read_preferences(s, env);
    browser_read_cache_dir(s, env);
    zip_cache_wipe_if_fresh(s->cache_root);
    s->manage_access = has_manage_storage_access(s, env);
    if (!s->permission_pending || permission_granted_worker(s, env)) {
        s->permission_pending = 0;
        choose_start_directory(s);
    } else {
        /* Build the browser while the permission dialog is pending. */
        choose_start_directory(s);
        set_message(s, "BITTE SPEICHERZUGRIFF ERLAUBEN");
        s->redraw = 1;
    }

    while (s->running) {
        if (s->resume_check) {
            char current[MAX_PATH];
            int granted;
            s->resume_check = 0;
            granted = has_manage_storage_access(s, env);
            if (granted != s->manage_access || s->manage_settings_open) {
                s->manage_access = granted;
                s->manage_settings_open = 0;
                copy_text(current, s->directory, MAX_PATH);
                if (browser_target_readable(current))
                    scan_directory(s, current);
                else
                    choose_start_directory(s);
            }
        }
        if (s->permission_pending && ++s->permission_poll >= 16) {
            s->permission_poll = 0;
            if (permission_granted_worker(s, env)) {
                s->permission_pending = 0;
                choose_start_directory(s);
            }
        }
        process_input(s, env, w, h);
        if (s->directory_dirty && !s->permission_pending) {
            s->directory_dirty = 0;
            browser_save_last_directory(s, env);
        }
        if (s->trim_requested) {
            s->trim_requested = 0;
            free_thumbnails(s);
            s->redraw = 1;
        }
        if (!s->paused && s->window)
            browser_fling_tick(s, monotonicish_millis(), max_scroll_for(s, w, h));
        else { s->fling_velocity = 0; s->touching = 0; }
        render_if_needed(s, &w, &h);
        if (!s->permission_pending && s->item_count > 0
                && s->window && !s->paused) {
            uint32_t now;
            visible_range(s, w, h, &first, &last);
            load_one_thumbnail(s, first, last);
            now = monotonicish_millis();
            animate_visible_thumbnails(s, first, last, now);
        }
        usleep(16000);
    }
    (*s->activity->vm)->DetachCurrentThread(s->activity->vm);
    return 0;
}

static void cb_destroy(ANativeActivity* activity) {
    BrowserState* s = (BrowserState*)activity->instance;
    if (!s) return;
    s->running = 0;
    s->window = 0;
    s->input = 0;
    pthread_join(s->thread, 0);
    while (s->draw_busy || s->input_busy) usleep(1000);
    free_thumbnails(s);
    if (s->zip) zip_archive_free(s->zip);
    free(s);
    activity->instance = 0;
}
static void cb_resume(ANativeActivity* a) {
    BrowserState* s = (BrowserState*)a->instance;
    if (!s) return;
    s->paused = 0;
    s->resume_check = 1;
    s->redraw = 1;
}
static void cb_pause(ANativeActivity* a) {
    BrowserState* s = (BrowserState*)a->instance;
    if (!s) return;
    s->paused = 1; /* worker owns touch/fling state and clears it while paused */
}
static void cb_window_created(ANativeActivity* a, ANativeWindow* w) {
    BrowserState* s = (BrowserState*)a->instance;
    s->window = w; s->redraw = 1;
}
static void cb_window_resized(ANativeActivity* a, ANativeWindow* w) {
    BrowserState* s = (BrowserState*)a->instance;
    s->window = w; s->redraw = 1;
}
static void cb_window_redraw(ANativeActivity* a, ANativeWindow* w) {
    BrowserState* s = (BrowserState*)a->instance;
    s->window = w; s->redraw = 1;
}
static void cb_window_destroyed(ANativeActivity* a, ANativeWindow* w) {
    BrowserState* s = (BrowserState*)a->instance;
    (void)w;
    s->window = 0;
    while (s->draw_busy) usleep(1000);
}
static void cb_input_created(ANativeActivity* a, AInputQueue* q) {
    BrowserState* s = (BrowserState*)a->instance;
    s->input = q;
}
static void cb_input_destroyed(ANativeActivity* a, AInputQueue* q) {
    BrowserState* s = (BrowserState*)a->instance;
    (void)q;
    s->input = 0;
    while (s->input_busy) usleep(1000);
}
static void cb_config_changed(ANativeActivity* a) {
    BrowserState* s = (BrowserState*)a->instance;
    s->redraw = 1;
}
static void cb_low_memory(ANativeActivity* a) {
    BrowserState* s = (BrowserState*)a->instance;
    /* The browser thread owns the item array and thumbnail pointers. */
    s->trim_requested = 1;
}

__attribute__((visibility("default")))
void ANativeActivity_onCreate(ANativeActivity* activity, void* savedState, size_t savedStateSize) {
    BrowserState* s = (BrowserState*)malloc(sizeof(BrowserState));
    (void)savedState; (void)savedStateSize;
    if (!s) { ANativeActivity_finish(activity); return; }
    if (!browser_load_android_api()) { free(s); ANativeActivity_finish(activity); return; }
    memset(s, 0, sizeof(BrowserState));
    s->animate_previews = 1;
    s->activity = activity;
    gallery_take_browser_return_request(s);
    gallery_read_browser_return_extras(activity, s);
    s->running = 1;
    s->redraw = 1;
    s->permission_pending = !request_storage_permission(activity);
    copy_text(s->directory, "SPEICHER", MAX_PATH);
    activity->instance = s;

    activity->callbacks->onDestroy = cb_destroy;
    activity->callbacks->onResume = cb_resume;
    activity->callbacks->onPause = cb_pause;
    activity->callbacks->onNativeWindowCreated = cb_window_created;
    activity->callbacks->onNativeWindowResized = cb_window_resized;
    activity->callbacks->onNativeWindowRedrawNeeded = cb_window_redraw;
    activity->callbacks->onNativeWindowDestroyed = cb_window_destroyed;
    activity->callbacks->onInputQueueCreated = cb_input_created;
    activity->callbacks->onInputQueueDestroyed = cb_input_destroyed;
    activity->callbacks->onConfigurationChanged = cb_config_changed;
    activity->callbacks->onLowMemory = cb_low_memory;

    ANativeActivity_setWindowFormat(activity, WINDOW_FORMAT_RGBX_8888);
    if (pthread_create(&s->thread, 0, browser_thread, s) != 0) {
        free(s); activity->instance = 0; ANativeActivity_finish(activity);
    }
}

#include "eclair_browser.inc"
#include "crash_log.inc"
