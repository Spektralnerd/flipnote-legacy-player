/* NativeActivity/libandroid were introduced at API 9. Keep them optional so
 * loading the player JNI library on API 7/8 never requires libandroid.so. */
#if defined(FLIPBROWSER_HOST_TEST)
#include <dlfcn.h>
#else
extern void* dlopen(const char*, int);
extern void* dlsym(void*, const char*);
#endif
static void* browser_android_handle;
#define API_SLOT(ret, name, params) static ret (*p_##name) params
API_SLOT(void, ANativeActivity_finish, (ANativeActivity*));
API_SLOT(void, ANativeActivity_setWindowFormat, (ANativeActivity*, int32_t));
API_SLOT(int32_t, ANativeWindow_lock, (ANativeWindow*, ANativeWindow_Buffer*, ARect*));
API_SLOT(int32_t, ANativeWindow_unlockAndPost, (ANativeWindow*));
API_SLOT(int32_t, AInputQueue_getEvent, (AInputQueue*, AInputEvent**));
API_SLOT(int32_t, AInputQueue_preDispatchEvent, (AInputQueue*, AInputEvent*));
API_SLOT(void, AInputQueue_finishEvent, (AInputQueue*, AInputEvent*, int));
API_SLOT(int32_t, AInputEvent_getType, (const AInputEvent*));
API_SLOT(int32_t, AKeyEvent_getAction, (const AInputEvent*));
API_SLOT(int32_t, AKeyEvent_getKeyCode, (const AInputEvent*));
API_SLOT(int32_t, AMotionEvent_getAction, (const AInputEvent*));
API_SLOT(int64_t, AMotionEvent_getEventTime, (const AInputEvent*));
API_SLOT(float, AMotionEvent_getX, (const AInputEvent*, size_t));
API_SLOT(float, AMotionEvent_getY, (const AInputEvent*, size_t));
#undef API_SLOT
static int browser_load_android_api(void) {
    int good = 1;
    if (!browser_android_handle) browser_android_handle = dlopen("libandroid.so", 2);
    if (!browser_android_handle) return 0;
#define LOAD(name) do { p_##name = (void*)dlsym(browser_android_handle, #name); if (!p_##name) good = 0; } while (0)
    LOAD(ANativeActivity_finish); LOAD(ANativeActivity_setWindowFormat);
    LOAD(ANativeWindow_lock); LOAD(ANativeWindow_unlockAndPost);
    LOAD(AInputQueue_getEvent); LOAD(AInputQueue_preDispatchEvent); LOAD(AInputQueue_finishEvent);
    LOAD(AInputEvent_getType); LOAD(AKeyEvent_getAction); LOAD(AKeyEvent_getKeyCode);
    LOAD(AMotionEvent_getAction); LOAD(AMotionEvent_getEventTime);
    LOAD(AMotionEvent_getX); LOAD(AMotionEvent_getY);
#undef LOAD
    return good;
}
void ANativeActivity_finish(ANativeActivity* a) {
    JNIEnv* env = 0;
    if (a->sdkVersion >= 9 && p_ANativeActivity_finish) { p_ANativeActivity_finish(a); return; }
    if ((*a->vm)->GetEnv(a->vm, (void**)&env, JNI_VERSION_1_4) == JNI_OK && env) {
        jclass cls = (*env)->FindClass(env, "android/app/Activity");
        jmethodID finish = cls ? (*env)->GetMethodID(env, cls, "finish", "()V") : 0;
        if (finish) (*env)->CallVoidMethod(env, a->clazz, finish);
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        if (cls) (*env)->DeleteLocalRef(env, cls);
    }
}
void ANativeActivity_setWindowFormat(ANativeActivity* a, int32_t f) { if (p_ANativeActivity_setWindowFormat) p_ANativeActivity_setWindowFormat(a,f); }
int32_t ANativeWindow_lock(ANativeWindow* w, ANativeWindow_Buffer* b, ARect* r) { return p_ANativeWindow_lock ? p_ANativeWindow_lock(w,b,r) : -1; }
int32_t ANativeWindow_unlockAndPost(ANativeWindow* w) { return p_ANativeWindow_unlockAndPost ? p_ANativeWindow_unlockAndPost(w) : -1; }
int32_t AInputQueue_getEvent(AInputQueue* q, AInputEvent** e) { return p_AInputQueue_getEvent ? p_AInputQueue_getEvent(q,e) : -1; }
int32_t AInputQueue_preDispatchEvent(AInputQueue* q, AInputEvent* e) { return p_AInputQueue_preDispatchEvent ? p_AInputQueue_preDispatchEvent(q,e) : 0; }
void AInputQueue_finishEvent(AInputQueue* q, AInputEvent* e, int h) { if(p_AInputQueue_finishEvent) p_AInputQueue_finishEvent(q,e,h); }
int32_t AInputEvent_getType(const AInputEvent* e) { return p_AInputEvent_getType ? p_AInputEvent_getType(e) : 0; }
int32_t AKeyEvent_getAction(const AInputEvent* e) { return p_AKeyEvent_getAction ? p_AKeyEvent_getAction(e) : 0; }
int32_t AKeyEvent_getKeyCode(const AInputEvent* e) { return p_AKeyEvent_getKeyCode ? p_AKeyEvent_getKeyCode(e) : 0; }
int32_t AMotionEvent_getAction(const AInputEvent* e) { return p_AMotionEvent_getAction ? p_AMotionEvent_getAction(e) : 0; }
int64_t AMotionEvent_getEventTime(const AInputEvent* e) { return p_AMotionEvent_getEventTime ? p_AMotionEvent_getEventTime(e) : 0; }
float AMotionEvent_getX(const AInputEvent* e, size_t n) { return p_AMotionEvent_getX ? p_AMotionEvent_getX(e,n) : 0; }
float AMotionEvent_getY(const AInputEvent* e, size_t n) { return p_AMotionEvent_getY ? p_AMotionEvent_getY(e,n) : 0; }
