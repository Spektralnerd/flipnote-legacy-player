/*
 * API-7/8 browser host: ordinary Activity + ImageView + Bitmap, all APIs >= 1.
 * API >= 9 is forwarded to the existing NativeActivity using FORWARD_RESULT.
 * The file list, previews, menu, selection and fling implementation are shared.
 * This path does not call, load or link libandroid.so on Android 2.1/2.2.
 */
typedef struct EclairBrowser {
    ANativeActivity activity;
    BrowserState state;
    jobject image;
    jobject bitmap;
    jintArray pixels;
    uint32_t* buffer;
    int width;
    int height;
    int started;
    int resumed;
} EclairBrowser;

static jfieldID eclair_handle_field(JNIEnv* env, jobject self) {
    jclass cls = (*env)->GetObjectClass(env, self);
    jfieldID field = cls ? (*env)->GetFieldID(env, cls, "nativeHandle", "J") : 0;
    if (cls) (*env)->DeleteLocalRef(env, cls);
    return field;
}
static EclairBrowser* eclair_get(JNIEnv* env, jobject self) {
    jfieldID field = eclair_handle_field(env, self);
    return field ? (EclairBrowser*)(size_t)(*env)->GetLongField(env, self, field) : 0;
}
static void eclair_super(JNIEnv* env, jobject self, const char* name) {
    jclass cls = (*env)->FindClass(env, "android/app/Activity");
    jmethodID method = cls ? (*env)->GetMethodID(env, cls, name, "()V") : 0;
    if (method) (*env)->CallNonvirtualVoidMethod(env, self, cls, method);
    if (cls) (*env)->DeleteLocalRef(env, cls);
}
static int eclair_sdk(JNIEnv* env) {
    int value = 7;
    jclass cls = (*env)->FindClass(env, "android/os/Build$VERSION");
    jfieldID field = cls ? (*env)->GetStaticFieldID(env, cls, "SDK_INT", "I") : 0;
    if (field) value = (*env)->GetStaticIntField(env, cls, field);
    jni_clear(env);
    if (cls) (*env)->DeleteLocalRef(env, cls);
    return value;
}
static void eclair_schedule(JNIEnv* env, jobject self, EclairBrowser* b, int delay) {
    jclass cls;
    jmethodID post;
    if (!b || !b->resumed || !b->image || b->state.finishing) return;
    cls = (*env)->FindClass(env, "android/view/View");
    post = cls ? (*env)->GetMethodID(env, cls, "postDelayed", "(Ljava/lang/Runnable;J)Z") : 0;
    if (post) (*env)->CallBooleanMethod(env, b->image, post, self, (jlong)delay);
    jni_clear(env);
    if (cls) (*env)->DeleteLocalRef(env, cls);
}
static void eclair_unschedule(JNIEnv* env, jobject self, EclairBrowser* b) {
    jclass cls;
    jmethodID remove;
    if (!b || !b->image) return;
    cls = (*env)->FindClass(env, "android/view/View");
    remove = cls ? (*env)->GetMethodID(env, cls, "removeCallbacks", "(Ljava/lang/Runnable;)Z") : 0;
    if (remove) (*env)->CallBooleanMethod(env, b->image, remove, self);
    jni_clear(env);
    if (cls) (*env)->DeleteLocalRef(env, cls);
}
static void eclair_finish_error(JNIEnv* env, jobject self) {
    jclass cls = (*env)->FindClass(env, "android/app/Activity");
    jmethodID finish = cls ? (*env)->GetMethodID(env, cls, "finish", "()V") : 0;
    jni_clear(env);
    if (finish) (*env)->CallVoidMethod(env, self, finish);
    if (cls) (*env)->DeleteLocalRef(env, cls);
}
static int eclair_forward_modern(JNIEnv* env, jobject self) {
    int success = 0;
    jclass activity = (*env)->FindClass(env, "android/app/Activity");
    jclass intent_class = (*env)->FindClass(env, "android/content/Intent");
    jmethodID get = activity ? (*env)->GetMethodID(env, activity, "getIntent", "()Landroid/content/Intent;") : 0;
    jmethodID start = activity ? (*env)->GetMethodID(env, activity, "startActivity", "(Landroid/content/Intent;)V") : 0;
    jmethodID set = intent_class ? (*env)->GetMethodID(env, intent_class, "setClassName", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;") : 0;
    jmethodID flags = intent_class ? (*env)->GetMethodID(env, intent_class, "addFlags", "(I)Landroid/content/Intent;") : 0;
    jobject intent = get ? (*env)->CallObjectMethod(env, self, get) : 0;
    jstring pkg = (*env)->NewStringUTF(env, "de.offlineflipnote.legacy");
    jstring cls = (*env)->NewStringUTF(env, "android.app.NativeActivity");
    if (intent && pkg && cls && start && set && flags) {
        (*env)->CallObjectMethod(env, intent, set, pkg, cls);
        (*env)->CallObjectMethod(env, intent, flags, 0x02000000); /* FLAG_ACTIVITY_FORWARD_RESULT */
        (*env)->CallVoidMethod(env, self, start, intent);
        success = !(*env)->ExceptionCheck(env);
    }
    jni_clear(env);
    if (success) eclair_finish_error(env, self);
    return success;
}

__attribute__((visibility("default")))
void Java_de_offlineflipnote_legacy_BrowserActivity_onCreate(JNIEnv* env, jobject self, jobject bundle) {
    EclairBrowser* b;
    jclass activity, image_class, view_class;
    jmethodID super_create, ctor, set_content, set_touch, set_bg;
    jobject image;
    int sdk;
    if ((*env)->PushLocalFrame(env, 48) < 0) return;
    activity = (*env)->FindClass(env, "android/app/Activity");
    super_create = activity ? (*env)->GetMethodID(env, activity, "onCreate", "(Landroid/os/Bundle;)V") : 0;
    if (super_create) (*env)->CallNonvirtualVoidMethod(env, self, activity, super_create, bundle);
    if ((*env)->ExceptionCheck(env)) { (*env)->PopLocalFrame(env, 0); return; }
    sdk = eclair_sdk(env);
    if (sdk >= 9) {
        if (!eclair_forward_modern(env, self)) eclair_finish_error(env, self);
        (*env)->PopLocalFrame(env, 0); return;
    }
    b = (EclairBrowser*)malloc(sizeof(EclairBrowser));
    if (!b) { eclair_finish_error(env, self); (*env)->PopLocalFrame(env, 0); return; }
    memset(b, 0, sizeof(*b));
    (*env)->GetJavaVM(env, &b->activity.vm);
    b->activity.env = env;
    b->activity.clazz = (*env)->NewGlobalRef(env, self);
    b->activity.sdkVersion = sdk;
    b->activity.instance = &b->state;
    b->state.activity = &b->activity;
    b->state.running = 1; b->state.paused = 1; b->state.redraw = 1;
    (*env)->SetLongField(env, self, eclair_handle_field(env,self), (jlong)(size_t)b);
    browser_read_preferences(&b->state, env);
    browser_read_cache_dir(&b->state, env);
    zip_cache_wipe_if_fresh(b->state.cache_root);
    gallery_take_browser_return_request(&b->state);
    gallery_read_browser_return_extras(&b->activity, &b->state);
    image_class = (*env)->FindClass(env, "android/widget/ImageView");
    view_class = (*env)->FindClass(env, "android/view/View");
    ctor = image_class ? (*env)->GetMethodID(env, image_class, "<init>", "(Landroid/content/Context;)V") : 0;
    set_content = activity ? (*env)->GetMethodID(env, activity, "setContentView", "(Landroid/view/View;)V") : 0;
    set_touch = view_class ? (*env)->GetMethodID(env, view_class, "setOnTouchListener", "(Landroid/view/View$OnTouchListener;)V") : 0;
    set_bg = view_class ? (*env)->GetMethodID(env, view_class, "setBackgroundColor", "(I)V") : 0;
    image = ctor ? (*env)->NewObject(env, image_class, ctor, self) : 0;
    if (image && set_content && set_touch && set_bg) {
        b->image = (*env)->NewGlobalRef(env, image);
        (*env)->CallVoidMethod(env, image, set_touch, self);
        (*env)->CallVoidMethod(env, image, set_bg, (jint)0xff111318);
        (*env)->CallVoidMethod(env, self, set_content, image);
    } else { jni_clear(env); eclair_finish_error(env,self); }
    (*env)->PopLocalFrame(env, 0);
}

static int eclair_resize(JNIEnv* env, EclairBrowser* b, int w, int h) {
    jclass bitmap_cls, config_cls, image_cls;
    jmethodID create, set;
    jfieldID config_field;
    jobject config, bitmap;
    jintArray pixels;
    uint32_t* buffer;
    if (w <= 0 || h <= 0 || w > 2048 || h > 2048) return 0;
    if (w == b->width && h == b->height && b->buffer) return 1;
    bitmap_cls = (*env)->FindClass(env, "android/graphics/Bitmap");
    config_cls = (*env)->FindClass(env, "android/graphics/Bitmap$Config");
    image_cls = (*env)->FindClass(env, "android/widget/ImageView");
    create = bitmap_cls ? (*env)->GetStaticMethodID(env, bitmap_cls, "createBitmap", "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;") : 0;
    config_field = config_cls ? (*env)->GetStaticFieldID(env, config_cls, "ARGB_8888", "Landroid/graphics/Bitmap$Config;") : 0;
    config = config_field ? (*env)->GetStaticObjectField(env, config_cls, config_field) : 0;
    set = image_cls ? (*env)->GetMethodID(env, image_cls, "setImageBitmap", "(Landroid/graphics/Bitmap;)V") : 0;
    if (!create || !config || !set) { jni_clear(env); return 0; }
    pixels = (*env)->NewIntArray(env, w*h);
    bitmap = pixels ? (*env)->CallStaticObjectMethod(env, bitmap_cls, create, w, h, config) : 0;
    if ((*env)->ExceptionCheck(env) || !pixels || !bitmap) { jni_clear(env); return 0; }
    buffer = (uint32_t*)malloc((size_t)w*h*sizeof(uint32_t));
    if (!buffer) return 0;
    if (b->pixels) (*env)->DeleteGlobalRef(env, b->pixels);
    if (b->bitmap) (*env)->DeleteGlobalRef(env, b->bitmap);
    if (b->buffer) free(b->buffer);
    b->pixels = (*env)->NewGlobalRef(env, pixels);
    b->bitmap = (*env)->NewGlobalRef(env, bitmap);
    b->buffer = buffer; b->width=w; b->height=h;
    b->state.fling_velocity=0;
    (*env)->CallVoidMethod(env, b->image, set, bitmap);
    b->state.redraw=1;
    return 1;
}

__attribute__((visibility("default")))
void Java_de_offlineflipnote_legacy_BrowserActivity_run(JNIEnv* env, jobject self) {
    EclairBrowser* b = eclair_get(env,self);
    jclass view_cls, bitmap_cls;
    jmethodID getw, geth, set_pixels, invalidate;
    int w,h,first,last;
    Canvas canvas;
    if (!b || !b->resumed || b->state.finishing) return;
    if ((*env)->PushLocalFrame(env,48) < 0) { jni_clear(env); return; }
    view_cls = (*env)->FindClass(env,"android/view/View");
    bitmap_cls = (*env)->FindClass(env,"android/graphics/Bitmap");
    getw = (*env)->GetMethodID(env,view_cls,"getWidth","()I");
    geth = (*env)->GetMethodID(env,view_cls,"getHeight","()I");
    w = (*env)->CallIntMethod(env,b->image,getw);
    h = (*env)->CallIntMethod(env,b->image,geth);
    if (eclair_resize(env,b,w,h)) {
        if (!b->started) { b->started=1; choose_start_directory(&b->state); }
        if (b->state.directory_dirty) {
            b->state.directory_dirty=0; browser_save_last_directory(&b->state,env);
        }
        if (b->state.trim_requested) {
            b->state.trim_requested=0; free_thumbnails(&b->state); b->state.redraw=1;
        }
        if (b->state.reveal_selected) {
            ensure_selection_visible(&b->state,w,h); b->state.reveal_selected=0;
        }
        browser_fling_tick(&b->state,monotonicish_millis(),max_scroll_for(&b->state,w,h));
        visible_range(&b->state,w,h,&first,&last);
        load_one_thumbnail(&b->state,first,last);
        animate_visible_thumbnails(&b->state,first,last,monotonicish_millis());
        if (b->state.redraw) {
            canvas.width=w; canvas.height=h; canvas.stride=w; canvas.format=100; canvas.bits=b->buffer;
            draw_browser(&b->state,&canvas);
            (*env)->SetIntArrayRegion(env,b->pixels,0,w*h,(const jint*)b->buffer);
            set_pixels = (*env)->GetMethodID(env,bitmap_cls,"setPixels","([IIIIIII)V");
            invalidate = (*env)->GetMethodID(env,view_cls,"invalidate","()V");
            if (set_pixels && invalidate) {
                (*env)->CallVoidMethod(env,b->bitmap,set_pixels,b->pixels,0,w,0,0,w,h);
                (*env)->CallVoidMethod(env,b->image,invalidate);
                b->state.redraw=0;
            }
        }
    }
    jni_clear(env);
    (*env)->PopLocalFrame(env,0);
    eclair_schedule(env,self,b,16);
}

__attribute__((visibility("default")))
jboolean Java_de_offlineflipnote_legacy_BrowserActivity_onTouch(JNIEnv* env,jobject self,jobject view,jobject event) {
    EclairBrowser* b=eclair_get(env,self);
    jclass cls;
    jmethodID action,x,y,time;
    int a,xx,yy;
    uint32_t t;
    (void)view;
    if (!b || !b->resumed) return JNI_FALSE;
    cls=(*env)->FindClass(env,"android/view/MotionEvent");
    action=(*env)->GetMethodID(env,cls,"getAction","()I");
    x=(*env)->GetMethodID(env,cls,"getX","()F");
    y=(*env)->GetMethodID(env,cls,"getY","()F");
    time=(*env)->GetMethodID(env,cls,"getEventTime","()J");
    a=(*env)->CallIntMethod(env,event,action)&255;
    xx=float_bits_to_int((*env)->CallFloatMethod(env,event,x));
    yy=float_bits_to_int((*env)->CallFloatMethod(env,event,y));
    t=(uint32_t)(*env)->CallLongMethod(env,event,time);
    (*env)->DeleteLocalRef(env,cls);
    browser_handle_motion(&b->state,env,a,xx,yy,b->width,b->height,t);
    return JNI_TRUE;
}

__attribute__((visibility("default")))
jboolean Java_de_offlineflipnote_legacy_BrowserActivity_onKeyDown(JNIEnv* env,jobject self,jint key,jobject event) {
    EclairBrowser* b=eclair_get(env,self);
    jclass cls;
    jmethodID method;
    jboolean result=JNI_FALSE;
    if (b && (key==AKEYCODE_BACK || key==82 || (key>=19 && key<=23) || key==66)) {
        handle_key(&b->state,env,key,b->width,b->height); return JNI_TRUE;
    }
    cls=(*env)->FindClass(env,"android/app/Activity");
    method=cls?(*env)->GetMethodID(env,cls,"onKeyDown","(ILandroid/view/KeyEvent;)Z"):0;
    if(method) result=(*env)->CallNonvirtualBooleanMethod(env,self,cls,method,key,event);
    if(cls) (*env)->DeleteLocalRef(env,cls);
    return result;
}
__attribute__((visibility("default")))
void Java_de_offlineflipnote_legacy_BrowserActivity_onResume(JNIEnv* env,jobject self) {
    EclairBrowser* b;
    eclair_super(env,self,"onResume"); b=eclair_get(env,self);
    if(b) { eclair_unschedule(env,self,b); b->resumed=1; b->state.paused=0; b->state.redraw=1; eclair_schedule(env,self,b,1); }
}
__attribute__((visibility("default")))
void Java_de_offlineflipnote_legacy_BrowserActivity_onPause(JNIEnv* env,jobject self) {
    EclairBrowser* b=eclair_get(env,self);
    if(b) { b->resumed=0; b->state.paused=1; b->state.fling_velocity=0; b->state.touching=0; eclair_unschedule(env,self,b); }
    eclair_super(env,self,"onPause");
}
__attribute__((visibility("default")))
void Java_de_offlineflipnote_legacy_BrowserActivity_onLowMemory(JNIEnv* env,jobject self) {
    EclairBrowser* b=eclair_get(env,self);
    if(b) b->state.trim_requested=1;
    eclair_super(env,self,"onLowMemory");
}
__attribute__((visibility("default")))
void Java_de_offlineflipnote_legacy_BrowserActivity_onDestroy(JNIEnv* env,jobject self) {
    EclairBrowser* b=eclair_get(env,self);
    if(b) {
        b->resumed=0; eclair_unschedule(env,self,b);
        free_thumbnails(&b->state);
        if(b->state.zip) zip_archive_free(b->state.zip);
        if(b->pixels) (*env)->DeleteGlobalRef(env,b->pixels);
        if(b->bitmap) (*env)->DeleteGlobalRef(env,b->bitmap);
        if(b->image) (*env)->DeleteGlobalRef(env,b->image);
        if(b->activity.clazz) (*env)->DeleteGlobalRef(env,b->activity.clazz);
        if(b->buffer) free(b->buffer);
        free(b);
        (*env)->SetLongField(env,self,eclair_handle_field(env,self),(jlong)0);
    }
    eclair_super(env,self,"onDestroy");
}
