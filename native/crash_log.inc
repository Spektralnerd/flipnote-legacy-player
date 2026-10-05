/*
 * Crash log (v1.4.15): an uncaught Java exception in the player is written
 * to "Flipnote-Absturz.txt" on the external storage (fallback: the app cache)
 * before Android's own handler closes the app. Old devices rarely offer a
 * logcat, so this is how a failure on Android 2.x can be reported.
 *
 * The handler is a tiny DEX class, CrashLog implements
 * Thread.UncaughtExceptionHandler, whose uncaughtException() is native. It is
 * installed from JNI_OnLoad, i.e. when MainActivity loads this library. The
 * previous default handler (Android's KillApplicationHandler) is kept and
 * called afterwards, so the normal "app stopped" dialog still appears.
 */
#define CRASH_LOG_NAME "Flipnote-Absturz.txt"
#define CRASH_LOG_FALLBACK "/data/data/de.offlineflipnote.legacy/cache/" CRASH_LOG_NAME
#define CRASH_LOG_MAX_BYTES (256 * 1024)
#define CRASH_TEXT_CAP (48 * 1024)

static jobject g_previous_crash_handler;   /* global ref or 0 */
static volatile int g_crash_log_busy;

typedef struct CrashText {
    char* data;
    int length;
} CrashText;

static void crash_append(CrashText* t, const char* s) {
    while (s && *s && t->length < CRASH_TEXT_CAP - 1) t->data[t->length++] = *s++;
    t->data[t->length] = 0;
}

static void crash_append_int(CrashText* t, int value) {
    char buf[16];
    int p = 0, n = value < 0 ? -value : value;
    if (value < 0) crash_append(t, "-");
    do { buf[p++] = (char)('0' + n % 10); n /= 10; } while (n && p < 15);
    while (p) { char one[2]; one[0] = buf[--p]; one[1] = 0; crash_append(t, one); }
}

static void crash_append_jstring(JNIEnv* env, CrashText* t, jstring value) {
    const char* utf;
    if (!value) return;
    utf = (*env)->GetStringUTFChars(env, value, 0);
    if (utf) {
        crash_append(t, utf);
        (*env)->ReleaseStringUTFChars(env, value, utf);
    }
    jni_clear(env);
}

/* Static String field of a framework class, or nothing. */
static void crash_append_static_string(JNIEnv* env, CrashText* t, const char* cls_name,
                                       const char* field) {
    jclass cls = (*env)->FindClass(env, cls_name);
    jfieldID id = cls ? (*env)->GetStaticFieldID(env, cls, field, "Ljava/lang/String;") : 0;
    jstring value = id ? (jstring)(*env)->GetStaticObjectField(env, cls, id) : 0;
    jni_clear(env);
    if (value) { crash_append_jstring(env, t, value); (*env)->DeleteLocalRef(env, value); }
    if (cls) (*env)->DeleteLocalRef(env, cls);
}

static int crash_static_int(JNIEnv* env, const char* cls_name, const char* field, int fallback) {
    jclass cls = (*env)->FindClass(env, cls_name);
    jfieldID id = cls ? (*env)->GetStaticFieldID(env, cls, field, "I") : 0;
    int value = id ? (int)(*env)->GetStaticIntField(env, cls, id) : fallback;
    jni_clear(env);
    if (cls) (*env)->DeleteLocalRef(env, cls);
    return value;
}

/* Log.getStackTraceString(Throwable) when android.util.Log exists (device),
   otherwise Throwable.toString() (desktop JVM in the host tests). */
static void crash_append_trace(JNIEnv* env, CrashText* t, jobject throwable) {
    jclass log_cls = (*env)->FindClass(env, "android/util/Log");
    jmethodID trace = log_cls ? (*env)->GetStaticMethodID(env, log_cls, "getStackTraceString",
            "(Ljava/lang/Throwable;)Ljava/lang/String;") : 0;
    jstring text = 0;
    jni_clear(env);
    if (trace) text = (jstring)(*env)->CallStaticObjectMethod(env, log_cls, trace, throwable);
    if ((*env)->ExceptionCheck(env)) { jni_clear(env); text = 0; }
    if (!text && throwable) {
        jclass obj_cls = (*env)->GetObjectClass(env, throwable);
        jmethodID to_string = obj_cls ? (*env)->GetMethodID(env, obj_cls, "toString", "()Ljava/lang/String;") : 0;
        if (to_string) text = (jstring)(*env)->CallObjectMethod(env, throwable, to_string);
        if ((*env)->ExceptionCheck(env)) { jni_clear(env); text = 0; }
        if (obj_cls) (*env)->DeleteLocalRef(env, obj_cls);
    }
    if (text) { crash_append_jstring(env, t, text); (*env)->DeleteLocalRef(env, text); }
    if (log_cls) (*env)->DeleteLocalRef(env, log_cls);
}

static void crash_append_thread_name(JNIEnv* env, CrashText* t, jobject thread) {
    jclass cls = thread ? (*env)->GetObjectClass(env, thread) : 0;
    jmethodID get_name = cls ? (*env)->GetMethodID(env, cls, "getName", "()Ljava/lang/String;") : 0;
    jstring name = get_name ? (jstring)(*env)->CallObjectMethod(env, thread, get_name) : 0;
    if ((*env)->ExceptionCheck(env)) { jni_clear(env); name = 0; }
    if (name) { crash_append_jstring(env, t, name); (*env)->DeleteLocalRef(env, name); }
    else crash_append(t, "?");
    if (cls) (*env)->DeleteLocalRef(env, cls);
}

/* <external storage>/Flipnote-Absturz.txt; empty when unavailable. */
static void crash_external_path(JNIEnv* env, char* out) {
    jclass env_cls = (*env)->FindClass(env, "android/os/Environment");
    jmethodID get_dir = env_cls ? (*env)->GetStaticMethodID(env, env_cls,
            "getExternalStorageDirectory", "()Ljava/io/File;") : 0;
    jobject dir = 0;
    jstring path = 0;
    out[0] = 0;
    jni_clear(env);
    if (get_dir) dir = (*env)->CallStaticObjectMethod(env, env_cls, get_dir);
    if ((*env)->ExceptionCheck(env)) { jni_clear(env); dir = 0; }
    if (dir) {
        jclass file_cls = (*env)->GetObjectClass(env, dir);
        jmethodID get_path = file_cls ? (*env)->GetMethodID(env, file_cls, "getAbsolutePath",
                "()Ljava/lang/String;") : 0;
        if (get_path) path = (jstring)(*env)->CallObjectMethod(env, dir, get_path);
        if ((*env)->ExceptionCheck(env)) { jni_clear(env); path = 0; }
        if (file_cls) (*env)->DeleteLocalRef(env, file_cls);
    }
    if (path) {
        const char* utf = (*env)->GetStringUTFChars(env, path, 0);
        if (utf && utf[0] == '/' && strlen(utf) + sizeof(CRASH_LOG_NAME) + 1 < MAX_PATH)
            join_path(out, utf, CRASH_LOG_NAME);
        if (utf) (*env)->ReleaseStringUTFChars(env, path, utf);
        (*env)->DeleteLocalRef(env, path);
    }
    if (dir) (*env)->DeleteLocalRef(env, dir);
    if (env_cls) (*env)->DeleteLocalRef(env, env_cls);
    jni_clear(env);
}

/* Appends to the log; starts over when it grew past CRASH_LOG_MAX_BYTES. */
static int crash_write(const char* path, const CrashText* t) {
    FILE* f;
    int existing = file_size32(path);
    if (!path[0]) return 0;
    f = fopen(path, existing > CRASH_LOG_MAX_BYTES ? "wb" : "ab");
    if (!f) return 0;
    fwrite(t->data, 1, (size_t)t->length, f);
    fclose(f);
    return 1;
}

static void crash_build_report(JNIEnv* env, CrashText* t, jobject thread, jobject throwable) {
    struct timeval tv;
    crash_append(t, "==== Flipnote Legacy Player 1.4.15 - Absturz ====\n");
    if (gettimeofday(&tv, 0) == 0) {
        crash_append(t, "Unix-Zeit: ");
        crash_append_int(t, (int)tv.tv_sec);
        crash_append(t, "\n");
    }
    crash_append(t, "Android API: ");
    crash_append_int(t, crash_static_int(env, "android/os/Build$VERSION", "SDK_INT", 0));
    crash_append(t, " (");
    crash_append_static_string(env, t, "android/os/Build$VERSION", "RELEASE");
    crash_append(t, ")\nGeraet: ");
    crash_append_static_string(env, t, "android/os/Build", "MANUFACTURER");
    crash_append(t, " ");
    crash_append_static_string(env, t, "android/os/Build", "MODEL");
    crash_append(t, "\nABI: ");
    crash_append_static_string(env, t, "android/os/Build", "CPU_ABI");
    crash_append(t, "\nThread: ");
    crash_append_thread_name(env, t, thread);
    crash_append(t, "\n");
    crash_append_trace(env, t, throwable);
    crash_append(t, "\n\n");
}

__attribute__((visibility("default")))
void Java_de_offlineflipnote_legacy_CrashLog_uncaughtException(
        JNIEnv* env, jobject self, jobject thread, jobject throwable) {
    CrashText text;
    char path[MAX_PATH];
    (void)self;
    if ((*env)->PushLocalFrame(env, 32) < 0) { jni_clear(env); return; }
    if (!try_lock(&g_crash_log_busy)) {
        text.data = (char*)malloc(CRASH_TEXT_CAP);
        text.length = 0;
        if (text.data) {
            text.data[0] = 0;
            crash_build_report(env, &text, thread, throwable);
            crash_external_path(env, path);
            if (!crash_write(path, &text)) crash_write(CRASH_LOG_FALLBACK, &text);
            free(text.data);
        }
        release_lock(&g_crash_log_busy);
    }
    jni_clear(env);
    /* Hand over to Android's handler so the process ends as usual. */
    if (g_previous_crash_handler) {
        jclass cls = (*env)->GetObjectClass(env, g_previous_crash_handler);
        jmethodID method = cls ? (*env)->GetMethodID(env, cls, "uncaughtException",
                "(Ljava/lang/Thread;Ljava/lang/Throwable;)V") : 0;
        if (method) (*env)->CallVoidMethod(env, g_previous_crash_handler, method, thread, throwable);
        jni_clear(env);
    }
    (*env)->PopLocalFrame(env, 0);
}

/* Called by System.loadLibrary() in MainActivity.<clinit>. Never fails:
   without the CrashLog class (desktop host test) the handler is skipped. */
__attribute__((visibility("default")))
jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    JNIEnv* env = 0;
    jclass log_cls, thread_cls;
    jmethodID ctor, get_default, set_default;
    jobject handler = 0, previous = 0;
    (void)reserved;
    if ((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_4) != JNI_OK || !env) return JNI_VERSION_1_4;
    if ((*env)->PushLocalFrame(env, 16) < 0) { jni_clear(env); return JNI_VERSION_1_4; }
    log_cls = (*env)->FindClass(env, "de/offlineflipnote/legacy/CrashLog");
    thread_cls = (*env)->FindClass(env, "java/lang/Thread");
    jni_clear(env);
    ctor = log_cls ? (*env)->GetMethodID(env, log_cls, "<init>", "()V") : 0;
    get_default = thread_cls ? (*env)->GetStaticMethodID(env, thread_cls,
            "getDefaultUncaughtExceptionHandler", "()Ljava/lang/Thread$UncaughtExceptionHandler;") : 0;
    set_default = thread_cls ? (*env)->GetStaticMethodID(env, thread_cls,
            "setDefaultUncaughtExceptionHandler", "(Ljava/lang/Thread$UncaughtExceptionHandler;)V") : 0;
    jni_clear(env);
    if (ctor && get_default && set_default && !g_previous_crash_handler) {
        handler = (*env)->NewObject(env, log_cls, ctor);
        previous = (*env)->CallStaticObjectMethod(env, thread_cls, get_default);
        if ((*env)->ExceptionCheck(env)) { jni_clear(env); handler = 0; }
        if (handler) {
            if (previous) g_previous_crash_handler = (*env)->NewGlobalRef(env, previous);
            (*env)->CallStaticVoidMethod(env, thread_cls, set_default, handler);
            jni_clear(env);
        }
    }
    (*env)->PopLocalFrame(env, 0);
    return JNI_VERSION_1_4;
}
