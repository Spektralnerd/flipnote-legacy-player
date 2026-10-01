/* ---- Gallery-style swipe pager for the Java player --------------------
 *
 * MainActivity is patched at DEX level to implement View.OnTouchListener,
 * load this native library and call galleryTick() from its existing UI
 * ticker. The browser supplies the ordered list of PPM files immediately
 * before returning the selected file to MainActivity.
 *
 * The existing PpmPlayerView remains the centre page, so the normal player
 * continues to provide audio, speed control, looping, seeking and menu
 * behaviour. Two ordinary PpmPlayerView instances render the silent previous
 * and next pages while the user drags. A HorizontalScrollView supplies a
 * clipping viewport which also works on Android 2.3 / API 9.
 */
#define GALLERY_SWIPE_THRESHOLD_MIN 48
#define GALLERY_SWIPE_THRESHOLD_MAX 220
#define GALLERY_SETTLE_MS 180
#define GALLERY_ZOOM_MIN 1000
#define GALLERY_ZOOM_MAX 8000
#define GALLERY_ZOOM_DOUBLE 2000
#define GALLERY_ZOOM_QUAD 4000
#define GALLERY_ZOOM_SLOP 8
#define GALLERY_WINDOW_FLAG_FULLSCREEN 0x00000400
#define GALLERY_UI_FLAG_LOW_PROFILE 0x00000001
#define GALLERY_UI_FLAG_HIDE_NAVIGATION 0x00000002
#define GALLERY_UI_FLAG_FULLSCREEN 0x00000004
#define GALLERY_UI_FLAG_LAYOUT_STABLE 0x00000100
#define GALLERY_UI_FLAG_LAYOUT_HIDE_NAVIGATION 0x00000200
#define GALLERY_UI_FLAG_LAYOUT_FULLSCREEN 0x00000400
#define GALLERY_UI_FLAG_IMMERSIVE_STICKY 0x00001000
#define GALLERY_SYSTEM_BARS_BEHAVIOR_TRANSIENT 2
#define GALLERY_CUTOUT_MODE_SHORT_EDGES 1
#define GALLERY_CUTOUT_MODE_ALWAYS 3
#define GALLERY_LEGACY_IMMERSIVE_RECHECK_MS 1200u
#define AMOTION_EVENT_ACTION_POINTER_DOWN 5
#define AMOTION_EVENT_ACTION_POINTER_UP 6

/* Written by the browser worker, consumed by MainActivity's UI thread.
   For a folder inside a ZIP archive, paths[] are the cache copies the Java
   player opens, locators[] say how to extract them on demand and
   virtual_directory keeps the archive folder for the browser return. */
typedef struct GallerySelection {
    volatile int lock;
    int generation;
    int count;
    int current;
    int from_zip;
    char zip_path[MAX_PATH];
    char virtual_directory[MAX_PATH];
    char paths[MAX_ITEMS][MAX_PATH];
    ZipLocator locators[MAX_ITEMS];
} GallerySelection;

/* Adjacent pages are Java PpmPlayerView/PpmFile objects. Their frame clocks
   are integer-only so the ARMv5 build needs no floating-point runtime. */
typedef struct GalleryPage {
    jobject view;
    jobject note;
    int valid;
    int frame_count;
    int frame_interval_ms;
    int frame;
    uint32_t next_due_ms;
} GalleryPage;

typedef struct GalleryRuntime {
    jobject activity;
    jobject container;
    jobject pager;
    jobject strip;
    jobject current_page;
    jobject current_view;
    jobject current_layout;
    jobject back_button;
    jobject zoom_button;
    jobject info_button;
    jobject seek_bar;
    jobject note_identity;
    GalleryPage previous;
    GalleryPage next;

    int attached;
    int generation;
    int count;
    int current;
    int list_matches_note;
    int from_zip;
    char zip_path[MAX_PATH];
    char virtual_directory[MAX_PATH];
    char paths[MAX_ITEMS][MAX_PATH];
    ZipLocator locators[MAX_ITEMS];

    int page_width;
    int page_height;
    int last_stretched;
    int back_visible;
    int zoom_visible;
    int info_visible;
    int zoom_label_milli;
    int external_intent_checked;
    int center_pending;

    int immersive_sdk;
    int immersive_applied;
    int immersive_had_focus;
    uint32_t immersive_last_check_ms;

    int zoom_milli;
    int zoom_pan_x;
    int zoom_pan_y;
    int zoom_left;
    int zoom_top;
    int zoom_scaled_width;
    int zoom_scaled_height;
    int pinching;
    int pinch_start_distance;
    int pinch_start_zoom;
    int pinch_content_x_milli;
    int pinch_content_y_milli;
    int zoom_panning;
    int pan_start_x;
    int pan_start_y;

    int touching;
    int horizontal_drag;
    int touch_down_x;
    int touch_down_y;
    int drag_x;
    uint32_t touch_down_ms;

    int settling;
    int settle_from;
    int settle_to;
    int settle_direction;
    uint32_t settle_start_ms;
} GalleryRuntime;

typedef struct BrowserReturnRequest {
    volatile int lock;
    int pending;
    char directory[MAX_PATH];
    char selected_path[MAX_PATH];
} BrowserReturnRequest;

static GallerySelection g_gallery_selection;
static GalleryRuntime g_gallery_runtime;
static BrowserReturnRequest g_browser_return_request;

static void gallery_selection_lock(void) {
    while (try_lock(&g_gallery_selection.lock)) usleep(1000);
}

static void gallery_selection_unlock(void) {
    release_lock(&g_gallery_selection.lock);
}

static void browser_return_lock(void) {
    while (try_lock(&g_browser_return_request.lock)) usleep(1000);
}

static void browser_return_unlock(void) {
    release_lock(&g_browser_return_request.lock);
}

static int gallery_prepare_browser_return_request(
        char* out_directory, char* out_selected_path) {
    const char* selected;
    char directory[MAX_PATH];
    char virtual_selected[MAX_PATH];
    if (!g_gallery_runtime.list_matches_note
            || g_gallery_runtime.current < 0
            || g_gallery_runtime.current >= g_gallery_runtime.count)
        return 0;
    selected = g_gallery_runtime.paths[g_gallery_runtime.current];
    if (!selected || selected[0] != '/') return 0;
    if (g_gallery_runtime.from_zip) {
        /* Return into the archive folder, not into the extraction cache. */
        if (g_gallery_runtime.virtual_directory[0] != '/') return 0;
        copy_text(directory, g_gallery_runtime.virtual_directory, MAX_PATH);
        join_path(virtual_selected, directory, path_basename(selected));
        selected = virtual_selected;
    } else {
        parent_path(selected, directory);
    }
    if (out_directory) copy_text(out_directory, directory, MAX_PATH);
    if (out_selected_path) copy_text(out_selected_path, selected, MAX_PATH);
    browser_return_lock();
    copy_text(g_browser_return_request.directory, directory, MAX_PATH);
    copy_text(g_browser_return_request.selected_path, selected, MAX_PATH);
    g_browser_return_request.pending = 1;
    browser_return_unlock();
    return 1;
}

static void gallery_cancel_browser_return_request(void) {
    browser_return_lock();
    g_browser_return_request.pending = 0;
    g_browser_return_request.directory[0] = 0;
    g_browser_return_request.selected_path[0] = 0;
    browser_return_unlock();
}

/* Called by the NativeActivity before its worker thread is started. */
static void gallery_take_browser_return_request(BrowserState* s) {
    browser_return_lock();
    if (g_browser_return_request.pending) {
        copy_text(s->restore_directory, g_browser_return_request.directory, MAX_PATH);
        copy_text(s->restore_path, g_browser_return_request.selected_path, MAX_PATH);
        s->restore_pending = 1;
        g_browser_return_request.pending = 0;
    }
    browser_return_unlock();
}

static const char* gallery_basename(const char* path) {
    const char* result = path;
    const char* p = path;
    while (p && *p) {
        if (*p == '/') result = p + 1;
        p++;
    }
    return result;
}

/* Capture only the actual PPM files, in the same sorted order as the grid.
   Entries of a ZIP folder are captured as their future cache copies plus the
   locator needed to extract each one when the pager reaches it. */
static void gallery_prepare_selection(BrowserState* s, int selected_item) {
    int i;
    int current = -1;
    int count = 0;
    int from_zip = 0;
    gallery_selection_lock();
    for (i = 0; i < s->item_count && count < MAX_ITEMS; i++) {
        BrowserItem* item = &s->items[i];
        if (item->is_access || item->is_system || item->is_dir) continue;
        if (item->in_zip) {
            const ZipLocator* loc = browser_item_locator(s, item);
            if (!loc || !zip_locator_supported(loc)) continue;
            if (!browser_item_cache_path(s, item, g_gallery_selection.paths[count])) continue;
            g_gallery_selection.locators[count] = *loc;
            from_zip = 1;
        } else {
            copy_text(g_gallery_selection.paths[count], item->path, MAX_PATH);
            memset(&g_gallery_selection.locators[count], 0, sizeof(ZipLocator));
        }
        if (i == selected_item) current = count;
        count++;
    }
    if (current < 0 || current >= count) {
        count = 0;
        current = 0;
        from_zip = 0;
    }
    g_gallery_selection.from_zip = from_zip;
    copy_text(g_gallery_selection.zip_path, from_zip && s->zip ? s->zip->path : "", MAX_PATH);
    copy_text(g_gallery_selection.virtual_directory, from_zip ? s->directory : "", MAX_PATH);
    g_gallery_selection.count = count;
    g_gallery_selection.current = current;
    g_gallery_selection.generation++;
    if (g_gallery_selection.generation <= 0) g_gallery_selection.generation = 1;
    gallery_selection_unlock();
}

/* Extracts the cache copy for a ZIP-backed page if it is missing. Regular
   files need nothing. Returns 1 when the path can be opened afterwards. */
static int gallery_materialize(int index) {
    if (index < 0 || index >= g_gallery_runtime.count) return 0;
    if (!g_gallery_runtime.from_zip) return 1;
    return zip_cache_ensure(g_gallery_runtime.paths[index],
                            g_gallery_runtime.zip_path,
                            &g_gallery_runtime.locators[index]);
}

static void gallery_delete_global(JNIEnv* env, jobject* ref) {
    if (*ref) {
        (*env)->DeleteGlobalRef(env, *ref);
        *ref = 0;
    }
}

static void gallery_clear_exception(JNIEnv* env) {
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
}

static jmethodID gallery_optional_method(
        JNIEnv* env, jclass cls, const char* name, const char* signature) {
    jmethodID method;
    if (!cls) return 0;
    method = (*env)->GetMethodID(env, cls, name, signature);
    if (!method) gallery_clear_exception(env);
    return method;
}

static jmethodID gallery_optional_static_method(
        JNIEnv* env, jclass cls, const char* name, const char* signature) {
    jmethodID method;
    if (!cls) return 0;
    method = (*env)->GetStaticMethodID(env, cls, name, signature);
    if (!method) gallery_clear_exception(env);
    return method;
}

static int gallery_android_sdk(JNIEnv* env) {
    jclass version_cls = 0;
    jfieldID sdk_field = 0;
    jint sdk = 9;
    if (g_gallery_runtime.immersive_sdk > 0)
        return g_gallery_runtime.immersive_sdk;
    version_cls = (*env)->FindClass(env, "android/os/Build$VERSION");
    if (!version_cls) {
        gallery_clear_exception(env);
        g_gallery_runtime.immersive_sdk = 9;
        return 9;
    }
    sdk_field = (*env)->GetStaticFieldID(env, version_cls, "SDK_INT", "I");
    if (sdk_field) {
        sdk = (*env)->GetStaticIntField(env, version_cls, sdk_field);
        if ((*env)->ExceptionCheck(env)) {
            gallery_clear_exception(env);
            sdk = 9;
        }
    } else {
        gallery_clear_exception(env);
    }
    (*env)->DeleteLocalRef(env, version_cls);
    if (sdk < 9) sdk = 9;
    g_gallery_runtime.immersive_sdk = (int)sdk;
    return (int)sdk;
}

static int gallery_legacy_ui_flags_for_sdk(int sdk) {
    int flags = 0;
    if (sdk >= 19) {
        flags = GALLERY_UI_FLAG_HIDE_NAVIGATION
              | GALLERY_UI_FLAG_FULLSCREEN
              | GALLERY_UI_FLAG_LAYOUT_STABLE
              | GALLERY_UI_FLAG_LAYOUT_HIDE_NAVIGATION
              | GALLERY_UI_FLAG_LAYOUT_FULLSCREEN
              | GALLERY_UI_FLAG_IMMERSIVE_STICKY;
    } else if (sdk >= 16) {
        flags = GALLERY_UI_FLAG_HIDE_NAVIGATION
              | GALLERY_UI_FLAG_FULLSCREEN
              | GALLERY_UI_FLAG_LAYOUT_STABLE
              | GALLERY_UI_FLAG_LAYOUT_HIDE_NAVIGATION
              | GALLERY_UI_FLAG_LAYOUT_FULLSCREEN;
    } else if (sdk >= 14) {
        flags = GALLERY_UI_FLAG_LOW_PROFILE
              | GALLERY_UI_FLAG_HIDE_NAVIGATION;
    } else if (sdk >= 11) {
        flags = GALLERY_UI_FLAG_LOW_PROFILE;
    }
    return flags;
}

/* Enter real immersive playback without adding a newer Java dependency.
   API 30+ uses WindowInsetsController through JNI, while Android 3.x-10 uses
   the legacy system-UI flags. Android 2.3 still receives the API-1 window
   fullscreen flag and therefore remains binary-compatible. */
static int gallery_apply_immersive(JNIEnv* env, jobject activity) {
    jclass activity_cls = 0;
    jclass window_cls = 0;
    jclass decor_cls = 0;
    jclass controller_cls = 0;
    jclass type_cls = 0;
    jmethodID get_window = 0;
    jmethodID set_flags = 0;
    jmethodID get_decor = 0;
    jmethodID set_ui = 0;
    jmethodID set_nav_color = 0;
    jmethodID set_status_color = 0;
    jmethodID set_nav_divider = 0;
    jmethodID set_nav_contrast = 0;
    jmethodID set_status_contrast = 0;
    jmethodID get_controller = 0;
    jmethodID set_behavior = 0;
    jmethodID hide = 0;
    jmethodID system_bars = 0;
    jmethodID get_attributes = 0;
    jmethodID set_attributes = 0;
    jmethodID set_decor_fits = 0;
    jmethodID set_fit_types = 0;
    jmethodID set_fit_sides = 0;
    jobject window = 0;
    jobject decor = 0;
    jobject controller = 0;
    jobject attributes = 0;
    jclass attributes_cls = 0;
    jfieldID cutout_mode_field = 0;
    jint types = 0;
    int sdk = gallery_android_sdk(env);
    int flags = gallery_legacy_ui_flags_for_sdk(sdk);
    int applied = 0;

    activity_cls = (*env)->GetObjectClass(env, activity);
    get_window = gallery_optional_method(env, activity_cls, "getWindow",
            "()Landroid/view/Window;");
    if (!get_window) goto done;
    window = (*env)->CallObjectMethod(env, activity, get_window);
    if ((*env)->ExceptionCheck(env) || !window) {
        gallery_clear_exception(env);
        goto done;
    }
    window_cls = (*env)->GetObjectClass(env, window);

    /* Hiding the bars alone does not make an old-targetSdk application use
       the display-cutout area. On modern phones this leaves exactly one safe
       inset visible: above the content in portrait and beside it in landscape.
       Request the entire physical display before the pager is measured. */
    if (sdk >= 28) {
        get_attributes = gallery_optional_method(env, window_cls,
                "getAttributes", "()Landroid/view/WindowManager$LayoutParams;");
        set_attributes = gallery_optional_method(env, window_cls,
                "setAttributes", "(Landroid/view/WindowManager$LayoutParams;)V");
        if (get_attributes && set_attributes) {
            attributes = (*env)->CallObjectMethod(env, window, get_attributes);
            if ((*env)->ExceptionCheck(env)) {
                gallery_clear_exception(env);
                attributes = 0;
            }
        }
        if (attributes) {
            attributes_cls = (*env)->GetObjectClass(env, attributes);
            cutout_mode_field = attributes_cls ? (*env)->GetFieldID(env,
                    attributes_cls, "layoutInDisplayCutoutMode", "I") : 0;
            if (!cutout_mode_field) gallery_clear_exception(env);
            if (cutout_mode_field) {
                (*env)->SetIntField(env, attributes, cutout_mode_field,
                        sdk >= 30 ? GALLERY_CUTOUT_MODE_ALWAYS
                                  : GALLERY_CUTOUT_MODE_SHORT_EDGES);
                gallery_clear_exception(env);
            }
            if (sdk >= 30 && attributes_cls) {
                /* These are the direct platform equivalents of asking a
                   non-floating window not to fit any system-bar insets. */
                set_fit_types = gallery_optional_method(env, attributes_cls,
                        "setFitInsetsTypes", "(I)V");
                set_fit_sides = gallery_optional_method(env, attributes_cls,
                        "setFitInsetsSides", "(I)V");
                if (set_fit_types)
                    (*env)->CallVoidMethod(env, attributes, set_fit_types, 0);
                if (set_fit_sides)
                    (*env)->CallVoidMethod(env, attributes, set_fit_sides, 0);
                gallery_clear_exception(env);
            }
            (*env)->CallVoidMethod(env, window, set_attributes, attributes);
            if (!(*env)->ExceptionCheck(env)) applied = 1;
            gallery_clear_exception(env);
        }
    }

    if (sdk >= 30) {
        set_decor_fits = gallery_optional_method(env, window_cls,
                "setDecorFitsSystemWindows", "(Z)V");
        if (set_decor_fits) {
            (*env)->CallVoidMethod(env, window, set_decor_fits, JNI_FALSE);
            if (!(*env)->ExceptionCheck(env)) applied = 1;
            gallery_clear_exception(env);
        }
    }

    /* FLAG_FULLSCREEN exists since API 1 and is the only fullscreen mechanism
       available to the Android-2.3-compatible Java player itself. */
    set_flags = gallery_optional_method(env, window_cls, "setFlags", "(II)V");
    if (set_flags) {
        (*env)->CallVoidMethod(env, window, set_flags,
                GALLERY_WINDOW_FLAG_FULLSCREEN, GALLERY_WINDOW_FLAG_FULLSCREEN);
        gallery_clear_exception(env);
        applied = 1;
    }

    get_decor = gallery_optional_method(env, window_cls, "getDecorView",
            "()Landroid/view/View;");
    if (get_decor) {
        decor = (*env)->CallObjectMethod(env, window, get_decor);
        if ((*env)->ExceptionCheck(env)) {
            gallery_clear_exception(env);
            decor = 0;
        }
    }
    if (decor && flags) {
        decor_cls = (*env)->GetObjectClass(env, decor);
        set_ui = gallery_optional_method(env, decor_cls,
                "setSystemUiVisibility", "(I)V");
        if (set_ui) {
            (*env)->CallVoidMethod(env, decor, set_ui, flags);
            gallery_clear_exception(env);
            applied = 1;
        }
    }

    /* Keep transient bar backgrounds black on OLED displays. These methods
       are invoked only on platform versions where they exist. */
    if (sdk >= 21) {
        set_nav_color = gallery_optional_method(env, window_cls,
                "setNavigationBarColor", "(I)V");
        set_status_color = gallery_optional_method(env, window_cls,
                "setStatusBarColor", "(I)V");
        if (set_nav_color)
            (*env)->CallVoidMethod(env, window, set_nav_color, (jint)0xff000000u);
        if (set_status_color)
            (*env)->CallVoidMethod(env, window, set_status_color, (jint)0xff000000u);
        gallery_clear_exception(env);
    }
    if (sdk >= 28) {
        set_nav_divider = gallery_optional_method(env, window_cls,
                "setNavigationBarDividerColor", "(I)V");
        if (set_nav_divider)
            (*env)->CallVoidMethod(env, window, set_nav_divider, (jint)0xff000000u);
        gallery_clear_exception(env);
    }
    if (sdk >= 29) {
        set_nav_contrast = gallery_optional_method(env, window_cls,
                "setNavigationBarContrastEnforced", "(Z)V");
        set_status_contrast = gallery_optional_method(env, window_cls,
                "setStatusBarContrastEnforced", "(Z)V");
        if (set_nav_contrast)
            (*env)->CallVoidMethod(env, window, set_nav_contrast, JNI_FALSE);
        if (set_status_contrast)
            (*env)->CallVoidMethod(env, window, set_status_contrast, JNI_FALSE);
        gallery_clear_exception(env);
    }

    /* Android 11 / API 30 introduced WindowInsetsController. Calling it in
       addition to the legacy flags reliably hides the gesture-navigation
       home indicator on current Android versions while keeping API 9 support. */
    if (sdk >= 30) {
        get_controller = gallery_optional_method(env, window_cls,
                "getInsetsController", "()Landroid/view/WindowInsetsController;");
        if (get_controller) {
            controller = (*env)->CallObjectMethod(env, window, get_controller);
            if ((*env)->ExceptionCheck(env)) {
                gallery_clear_exception(env);
                controller = 0;
            }
        }
        if (controller) {
            controller_cls = (*env)->GetObjectClass(env, controller);
            set_behavior = gallery_optional_method(env, controller_cls,
                    "setSystemBarsBehavior", "(I)V");
            hide = gallery_optional_method(env, controller_cls, "hide", "(I)V");
            type_cls = (*env)->FindClass(env, "android/view/WindowInsets$Type");
            if (!type_cls) gallery_clear_exception(env);
            system_bars = gallery_optional_static_method(env, type_cls,
                    "systemBars", "()I");
            if (system_bars) {
                types = (*env)->CallStaticIntMethod(env, type_cls, system_bars);
                if ((*env)->ExceptionCheck(env)) {
                    gallery_clear_exception(env);
                    types = 0;
                }
            }
            if (set_behavior)
                (*env)->CallVoidMethod(env, controller, set_behavior,
                        GALLERY_SYSTEM_BARS_BEHAVIOR_TRANSIENT);
            if (hide && types)
                (*env)->CallVoidMethod(env, controller, hide, types);
            if (!(*env)->ExceptionCheck(env) && hide && types) applied = 1;
            gallery_clear_exception(env);
        }
    }

done:
    if (attributes_cls) (*env)->DeleteLocalRef(env, attributes_cls);
    if (attributes) (*env)->DeleteLocalRef(env, attributes);
    if (type_cls) (*env)->DeleteLocalRef(env, type_cls);
    if (controller_cls) (*env)->DeleteLocalRef(env, controller_cls);
    if (controller) (*env)->DeleteLocalRef(env, controller);
    if (decor_cls) (*env)->DeleteLocalRef(env, decor_cls);
    if (decor) (*env)->DeleteLocalRef(env, decor);
    if (window_cls) (*env)->DeleteLocalRef(env, window_cls);
    if (window) (*env)->DeleteLocalRef(env, window);
    if (activity_cls) (*env)->DeleteLocalRef(env, activity_cls);
    gallery_clear_exception(env);
    if (applied) g_gallery_runtime.immersive_applied = 1;
    g_gallery_runtime.immersive_last_check_ms = monotonicish_millis();
    return applied;
}

static int gallery_activity_has_window_focus(JNIEnv* env, jobject activity) {
    jclass activity_cls = 0;
    jmethodID has_focus = 0;
    jboolean focused = JNI_FALSE;
    activity_cls = (*env)->GetObjectClass(env, activity);
    has_focus = gallery_optional_method(env, activity_cls,
            "hasWindowFocus", "()Z");
    if (has_focus) {
        focused = (*env)->CallBooleanMethod(env, activity, has_focus);
        if ((*env)->ExceptionCheck(env)) {
            gallery_clear_exception(env);
            focused = JNI_FALSE;
        }
    }
    if (activity_cls) (*env)->DeleteLocalRef(env, activity_cls);
    return focused == JNI_TRUE;
}

static int gallery_legacy_navigation_is_hidden(JNIEnv* env, jobject activity) {
    jclass activity_cls = 0;
    jclass window_cls = 0;
    jclass decor_cls = 0;
    jmethodID get_window = 0;
    jmethodID get_decor = 0;
    jmethodID get_ui = 0;
    jobject window = 0;
    jobject decor = 0;
    jint flags = 0;
    int hidden = 0;
    activity_cls = (*env)->GetObjectClass(env, activity);
    get_window = gallery_optional_method(env, activity_cls, "getWindow",
            "()Landroid/view/Window;");
    if (get_window) window = (*env)->CallObjectMethod(env, activity, get_window);
    if ((*env)->ExceptionCheck(env) || !window) {
        gallery_clear_exception(env);
        goto done;
    }
    window_cls = (*env)->GetObjectClass(env, window);
    get_decor = gallery_optional_method(env, window_cls, "getDecorView",
            "()Landroid/view/View;");
    if (get_decor) decor = (*env)->CallObjectMethod(env, window, get_decor);
    if ((*env)->ExceptionCheck(env) || !decor) {
        gallery_clear_exception(env);
        goto done;
    }
    decor_cls = (*env)->GetObjectClass(env, decor);
    get_ui = gallery_optional_method(env, decor_cls,
            "getSystemUiVisibility", "()I");
    if (get_ui) flags = (*env)->CallIntMethod(env, decor, get_ui);
    if ((*env)->ExceptionCheck(env)) {
        gallery_clear_exception(env);
        flags = 0;
    }
    hidden = (flags & GALLERY_UI_FLAG_HIDE_NAVIGATION) != 0;

done:
    if (decor_cls) (*env)->DeleteLocalRef(env, decor_cls);
    if (decor) (*env)->DeleteLocalRef(env, decor);
    if (window_cls) (*env)->DeleteLocalRef(env, window_cls);
    if (window) (*env)->DeleteLocalRef(env, window);
    if (activity_cls) (*env)->DeleteLocalRef(env, activity_cls);
    gallery_clear_exception(env);
    return hidden;
}

static void gallery_maintain_immersive(JNIEnv* env, jobject activity) {
    uint32_t now = monotonicish_millis();
    int sdk;
    int focused = gallery_activity_has_window_focus(env, activity);
    if (!focused) {
        g_gallery_runtime.immersive_had_focus = 0;
        return;
    }
    sdk = gallery_android_sdk(env);
    if (!g_gallery_runtime.immersive_applied
            || !g_gallery_runtime.immersive_had_focus) {
        gallery_apply_immersive(env, activity);
    } else if (sdk >= 14 && sdk < 19
            && now - g_gallery_runtime.immersive_last_check_ms
                    >= GALLERY_LEGACY_IMMERSIVE_RECHECK_MS) {
        /* Pre-KitKat has no sticky immersive mode; a normal touch reveals the
           software navigation bar. Re-hide it after a short grace period. */
        if (!gallery_legacy_navigation_is_hidden(env, activity))
            gallery_apply_immersive(env, activity);
        else
            g_gallery_runtime.immersive_last_check_ms = now;
    }
    g_gallery_runtime.immersive_had_focus = 1;
}

static void gallery_read_browser_return_extras(
        ANativeActivity* activity, BrowserState* state) {
    JNIEnv* env = activity->env;
    jclass activity_cls = 0;
    jclass intent_cls = 0;
    jmethodID get_intent = 0;
    jmethodID get_string_extra = 0;
    jobject intent = 0;
    jstring directory_key = 0;
    jstring path_key = 0;
    jstring directory_obj = 0;
    jstring path_obj = 0;
    const char* directory = 0;
    const char* path = 0;

    activity_cls = (*env)->GetObjectClass(env, activity->clazz);
    get_intent = activity_cls ? (*env)->GetMethodID(env, activity_cls,
            "getIntent", "()Landroid/content/Intent;") : 0;
    if (get_intent) intent = (*env)->CallObjectMethod(env, activity->clazz, get_intent);
    if ((*env)->ExceptionCheck(env) || !intent) {
        gallery_clear_exception(env);
        goto done;
    }
    intent_cls = (*env)->GetObjectClass(env, intent);
    get_string_extra = intent_cls ? (*env)->GetMethodID(env, intent_cls,
            "getStringExtra", "(Ljava/lang/String;)Ljava/lang/String;") : 0;
    if (!get_string_extra) { gallery_clear_exception(env); goto done; }
    directory_key = (*env)->NewStringUTF(env, "flipnote.browser.return.directory");
    path_key = (*env)->NewStringUTF(env, "flipnote.browser.return.path");
    if (!directory_key || !path_key) { gallery_clear_exception(env); goto done; }
    directory_obj = (jstring)(*env)->CallObjectMethod(env, intent,
            get_string_extra, directory_key);
    path_obj = (jstring)(*env)->CallObjectMethod(env, intent,
            get_string_extra, path_key);
    if ((*env)->ExceptionCheck(env)) { gallery_clear_exception(env); goto done; }
    directory = directory_obj ? (*env)->GetStringUTFChars(env, directory_obj, 0) : 0;
    path = path_obj ? (*env)->GetStringUTFChars(env, path_obj, 0) : 0;
    if (directory && path && directory[0] == '/' && path[0] == '/') {
        copy_text(state->restore_directory, directory, MAX_PATH);
        copy_text(state->restore_path, path, MAX_PATH);
        state->restore_pending = 1;
    }

done:
    if (path && path_obj) (*env)->ReleaseStringUTFChars(env, path_obj, path);
    if (directory && directory_obj)
        (*env)->ReleaseStringUTFChars(env, directory_obj, directory);
    if (path_obj) (*env)->DeleteLocalRef(env, path_obj);
    if (directory_obj) (*env)->DeleteLocalRef(env, directory_obj);
    if (path_key) (*env)->DeleteLocalRef(env, path_key);
    if (directory_key) (*env)->DeleteLocalRef(env, directory_key);
    if (intent_cls) (*env)->DeleteLocalRef(env, intent_cls);
    if (intent) (*env)->DeleteLocalRef(env, intent);
    if (activity_cls) (*env)->DeleteLocalRef(env, activity_cls);
    gallery_clear_exception(env);
}

static void gallery_page_forget_note(JNIEnv* env, GalleryPage* page) {
    if (page->note) {
        (*env)->DeleteGlobalRef(env, page->note);
        page->note = 0;
    }
    page->valid = 0;
    page->frame_count = 0;
    page->frame_interval_ms = 0;
    page->frame = 0;
    page->next_due_ms = 0;
}

static void gallery_page_set_empty(JNIEnv* env, GalleryPage* page) {
    jclass view_cls;
    jmethodID set_note;
    gallery_page_forget_note(env, page);
    if (!page->view) return;
    view_cls = (*env)->GetObjectClass(env, page->view);
    set_note = view_cls ? (*env)->GetMethodID(env, view_cls, "setNote",
            "(Lde/offlineflipnote/legacy/PpmFile;)V") : 0;
    if (set_note) (*env)->CallVoidMethod(env, page->view, set_note, 0);
    gallery_clear_exception(env);
    if (view_cls) (*env)->DeleteLocalRef(env, view_cls);
}

static void gallery_runtime_release(JNIEnv* env) {
    if (g_gallery_runtime.current_view) {
        jclass view_cls = (*env)->GetObjectClass(env, g_gallery_runtime.current_view);
        jmethodID set_listener = view_cls ? (*env)->GetMethodID(env, view_cls,
                "setOnTouchListener", "(Landroid/view/View$OnTouchListener;)V") : 0;
        if (set_listener)
            (*env)->CallVoidMethod(env, g_gallery_runtime.current_view, set_listener, 0);
        gallery_clear_exception(env);
        if (view_cls) (*env)->DeleteLocalRef(env, view_cls);
    }
    if (g_gallery_runtime.back_button) {
        jclass view_cls = (*env)->GetObjectClass(env, g_gallery_runtime.back_button);
        jmethodID set_listener = view_cls ? (*env)->GetMethodID(env, view_cls,
                "setOnTouchListener", "(Landroid/view/View$OnTouchListener;)V") : 0;
        if (set_listener)
            (*env)->CallVoidMethod(env, g_gallery_runtime.back_button, set_listener, 0);
        gallery_clear_exception(env);
        if (view_cls) (*env)->DeleteLocalRef(env, view_cls);
    }
    if (g_gallery_runtime.zoom_button) {
        jclass view_cls = (*env)->GetObjectClass(env, g_gallery_runtime.zoom_button);
        jmethodID set_listener = view_cls ? (*env)->GetMethodID(env, view_cls,
                "setOnTouchListener", "(Landroid/view/View$OnTouchListener;)V") : 0;
        if (set_listener)
            (*env)->CallVoidMethod(env, g_gallery_runtime.zoom_button, set_listener, 0);
        gallery_clear_exception(env);
        if (view_cls) (*env)->DeleteLocalRef(env, view_cls);
    }
    if (g_gallery_runtime.info_button) {
        jclass view_cls = (*env)->GetObjectClass(env, g_gallery_runtime.info_button);
        jmethodID set_listener = view_cls ? (*env)->GetMethodID(env, view_cls,
                "setOnTouchListener", "(Landroid/view/View$OnTouchListener;)V") : 0;
        if (set_listener)
            (*env)->CallVoidMethod(env, g_gallery_runtime.info_button, set_listener, 0);
        gallery_clear_exception(env);
        if (view_cls) (*env)->DeleteLocalRef(env, view_cls);
    }
    gallery_page_forget_note(env, &g_gallery_runtime.previous);
    gallery_page_forget_note(env, &g_gallery_runtime.next);
    gallery_delete_global(env, &g_gallery_runtime.previous.view);
    gallery_delete_global(env, &g_gallery_runtime.next.view);
    gallery_delete_global(env, &g_gallery_runtime.back_button);
    gallery_delete_global(env, &g_gallery_runtime.zoom_button);
    gallery_delete_global(env, &g_gallery_runtime.info_button);
    gallery_delete_global(env, &g_gallery_runtime.seek_bar);
    gallery_delete_global(env, &g_gallery_runtime.note_identity);
    gallery_delete_global(env, &g_gallery_runtime.current_layout);
    gallery_delete_global(env, &g_gallery_runtime.current_view);
    gallery_delete_global(env, &g_gallery_runtime.current_page);
    gallery_delete_global(env, &g_gallery_runtime.strip);
    gallery_delete_global(env, &g_gallery_runtime.pager);
    gallery_delete_global(env, &g_gallery_runtime.container);
    gallery_delete_global(env, &g_gallery_runtime.activity);
    memset(&g_gallery_runtime, 0, sizeof(g_gallery_runtime));
    g_gallery_runtime.last_stretched = -1;
}


static void gallery_clear_selection(void) {
    gallery_selection_lock();
    g_gallery_selection.count = 0;
    g_gallery_selection.current = 0;
    g_gallery_selection.from_zip = 0;
    g_gallery_selection.zip_path[0] = 0;
    g_gallery_selection.virtual_directory[0] = 0;
    g_gallery_selection.generation++;
    if (g_gallery_selection.generation <= 0) g_gallery_selection.generation = 1;
    gallery_selection_unlock();
}

typedef struct GalleryText {
    char* data;
    int length;
    int capacity;
} GalleryText;

static void gallery_text_append(GalleryText* out, const char* text) {
    int i = 0;
    if (!out || !out->data || out->capacity <= 0 || !text) return;
    while (text[i] && out->length < out->capacity - 1)
        out->data[out->length++] = text[i++];
    out->data[out->length] = 0;
}

static void gallery_text_append_int(GalleryText* out, int value) {
    char reverse[16];
    char forward[16];
    int count = 0;
    int pos = 0;
    unsigned int magnitude;
    if (value < 0) {
        gallery_text_append(out, "-");
        magnitude = (unsigned int)(-(value + 1)) + 1u;
    } else {
        magnitude = (unsigned int)value;
    }
    if (magnitude == 0) reverse[count++] = '0';
    while (magnitude && count < 15) {
        reverse[count++] = (char)('0' + (magnitude % 10u));
        magnitude /= 10u;
    }
    while (count > 0) forward[pos++] = reverse[--count];
    forward[pos] = 0;
    gallery_text_append(out, forward);
}


/* ---- Main-player zoom --------------------------------------------------
 *
 * The centre PpmPlayerView lives inside a fixed-size FrameLayout page. Zoom
 * is implemented by enlarging and repositioning that existing Java view, so
 * no new Java class and no API newer than Android 2.3 are required. The page
 * clips the enlarged view. At 100% the old gallery swipe gesture is active;
 * above 100% one-finger dragging pans the image instead.
 */
static int gallery_zoom_scaled_dimension(int base, int zoom_milli) {
    int scaled;
    if (base <= 0) return 0;
    zoom_milli = iclamp(zoom_milli, GALLERY_ZOOM_MIN, GALLERY_ZOOM_MAX);
    scaled = (base * zoom_milli + 500) / 1000;
    return scaled < base ? base : scaled;
}

static int gallery_zoom_distance(int x0, int y0, int x1, int y1) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    int large;
    int small;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    large = dx > dy ? dx : dy;
    small = dx > dy ? dy : dx;
    /* A sqrt-free approximation accurate enough for a pinch ratio and safe
       on the ARMv5 soft-float build. */
    return large + small / 2;
}

static void gallery_zoom_update_button_label(JNIEnv* env) {
    char text[20];
    GalleryText out;
    jclass cls;
    jmethodID set_text;
    jstring value;
    int zoom = g_gallery_runtime.zoom_milli;
    if (!g_gallery_runtime.zoom_button || zoom <= 0
            || zoom == g_gallery_runtime.zoom_label_milli)
        return;
    text[0] = 0;
    out.data = text;
    out.length = 0;
    out.capacity = (int)sizeof(text);
    if (zoom == 1000) gallery_text_append(&out, "1x");
    else if (zoom == 2000) gallery_text_append(&out, "2x");
    else if (zoom == 4000) gallery_text_append(&out, "4x");
    else if (zoom == 8000) gallery_text_append(&out, "8x");
    else {
        gallery_text_append_int(&out, (zoom + 5) / 10);
        gallery_text_append(&out, "%");
    }
    cls = (*env)->GetObjectClass(env, g_gallery_runtime.zoom_button);
    set_text = cls ? (*env)->GetMethodID(env, cls, "setText",
            "(Ljava/lang/CharSequence;)V") : 0;
    value = (*env)->NewStringUTF(env, text);
    if (set_text && value)
        (*env)->CallVoidMethod(env, g_gallery_runtime.zoom_button, set_text, value);
    gallery_clear_exception(env);
    if (value) (*env)->DeleteLocalRef(env, value);
    if (cls) (*env)->DeleteLocalRef(env, cls);
    g_gallery_runtime.zoom_label_milli = zoom;
}

static void gallery_zoom_clamp_pan(void) {
    int max_x;
    int max_y;
    g_gallery_runtime.zoom_scaled_width = gallery_zoom_scaled_dimension(
            g_gallery_runtime.page_width, g_gallery_runtime.zoom_milli);
    g_gallery_runtime.zoom_scaled_height = gallery_zoom_scaled_dimension(
            g_gallery_runtime.page_height, g_gallery_runtime.zoom_milli);
    max_x = (g_gallery_runtime.zoom_scaled_width - g_gallery_runtime.page_width) / 2;
    max_y = (g_gallery_runtime.zoom_scaled_height - g_gallery_runtime.page_height) / 2;
    g_gallery_runtime.zoom_pan_x = iclamp(g_gallery_runtime.zoom_pan_x, -max_x, max_x);
    g_gallery_runtime.zoom_pan_y = iclamp(g_gallery_runtime.zoom_pan_y, -max_y, max_y);
    g_gallery_runtime.zoom_left =
            (g_gallery_runtime.page_width - g_gallery_runtime.zoom_scaled_width) / 2
            + g_gallery_runtime.zoom_pan_x;
    g_gallery_runtime.zoom_top =
            (g_gallery_runtime.page_height - g_gallery_runtime.zoom_scaled_height) / 2
            + g_gallery_runtime.zoom_pan_y;
}

static void gallery_zoom_apply_layout(JNIEnv* env, int request_layout) {
    jclass lp_cls = 0;
    jclass view_cls = 0;
    jclass page_cls = 0;
    jfieldID width_field = 0, height_field = 0;
    jfieldID left_field = 0, top_field = 0, right_field = 0, bottom_field = 0;
    jmethodID set_lp = 0, layout = 0, invalidate = 0, page_invalidate = 0;
    if (!g_gallery_runtime.current_view || !g_gallery_runtime.current_layout
            || g_gallery_runtime.page_width <= 0 || g_gallery_runtime.page_height <= 0)
        return;
    if (g_gallery_runtime.zoom_milli <= 0)
        g_gallery_runtime.zoom_milli = GALLERY_ZOOM_MIN;
    gallery_zoom_clamp_pan();

    lp_cls = (*env)->GetObjectClass(env, g_gallery_runtime.current_layout);
    view_cls = (*env)->GetObjectClass(env, g_gallery_runtime.current_view);
    page_cls = g_gallery_runtime.current_page
            ? (*env)->GetObjectClass(env, g_gallery_runtime.current_page) : 0;
    width_field = lp_cls ? (*env)->GetFieldID(env, lp_cls, "width", "I") : 0;
    height_field = lp_cls ? (*env)->GetFieldID(env, lp_cls, "height", "I") : 0;
    left_field = lp_cls ? (*env)->GetFieldID(env, lp_cls, "leftMargin", "I") : 0;
    top_field = lp_cls ? (*env)->GetFieldID(env, lp_cls, "topMargin", "I") : 0;
    right_field = lp_cls ? (*env)->GetFieldID(env, lp_cls, "rightMargin", "I") : 0;
    bottom_field = lp_cls ? (*env)->GetFieldID(env, lp_cls, "bottomMargin", "I") : 0;
    set_lp = view_cls ? (*env)->GetMethodID(env, view_cls, "setLayoutParams",
            "(Landroid/view/ViewGroup$LayoutParams;)V") : 0;
    layout = view_cls ? (*env)->GetMethodID(env, view_cls, "layout", "(IIII)V") : 0;
    invalidate = view_cls ? (*env)->GetMethodID(env, view_cls, "invalidate", "()V") : 0;
    page_invalidate = page_cls ? (*env)->GetMethodID(env, page_cls, "invalidate", "()V") : 0;
    if (width_field) (*env)->SetIntField(env, g_gallery_runtime.current_layout,
            width_field, g_gallery_runtime.zoom_scaled_width);
    if (height_field) (*env)->SetIntField(env, g_gallery_runtime.current_layout,
            height_field, g_gallery_runtime.zoom_scaled_height);
    if (left_field) (*env)->SetIntField(env, g_gallery_runtime.current_layout,
            left_field, g_gallery_runtime.zoom_left);
    if (top_field) (*env)->SetIntField(env, g_gallery_runtime.current_layout,
            top_field, g_gallery_runtime.zoom_top);
    if (right_field) (*env)->SetIntField(env, g_gallery_runtime.current_layout,
            right_field, 0);
    if (bottom_field) (*env)->SetIntField(env, g_gallery_runtime.current_layout,
            bottom_field, 0);
    if (request_layout && set_lp)
        (*env)->CallVoidMethod(env, g_gallery_runtime.current_view, set_lp,
                              g_gallery_runtime.current_layout);
    if (layout)
        (*env)->CallVoidMethod(env, g_gallery_runtime.current_view, layout,
                g_gallery_runtime.zoom_left,
                g_gallery_runtime.zoom_top,
                g_gallery_runtime.zoom_left + g_gallery_runtime.zoom_scaled_width,
                g_gallery_runtime.zoom_top + g_gallery_runtime.zoom_scaled_height);
    if (invalidate) (*env)->CallVoidMethod(env, g_gallery_runtime.current_view, invalidate);
    if (page_invalidate && g_gallery_runtime.current_page)
        (*env)->CallVoidMethod(env, g_gallery_runtime.current_page, page_invalidate);
    gallery_clear_exception(env);
    if (page_cls) (*env)->DeleteLocalRef(env, page_cls);
    if (view_cls) (*env)->DeleteLocalRef(env, view_cls);
    if (lp_cls) (*env)->DeleteLocalRef(env, lp_cls);
    gallery_zoom_update_button_label(env);
}

static void gallery_zoom_set_around(
        JNIEnv* env, int new_zoom, int focus_x, int focus_y, int request_layout) {
    int old_zoom;
    int content_x_milli;
    int content_y_milli;
    int new_width;
    int new_height;
    int centered_left;
    int centered_top;
    int desired_left;
    int desired_top;
    if (g_gallery_runtime.page_width <= 0 || g_gallery_runtime.page_height <= 0)
        return;
    if (g_gallery_runtime.zoom_milli <= 0)
        g_gallery_runtime.zoom_milli = GALLERY_ZOOM_MIN;
    old_zoom = g_gallery_runtime.zoom_milli;
    gallery_zoom_clamp_pan();
    focus_x = iclamp(focus_x, 0, g_gallery_runtime.page_width);
    focus_y = iclamp(focus_y, 0, g_gallery_runtime.page_height);
    content_x_milli = ((focus_x - g_gallery_runtime.zoom_left) * 1000) / old_zoom;
    content_y_milli = ((focus_y - g_gallery_runtime.zoom_top) * 1000) / old_zoom;
    new_zoom = iclamp(new_zoom, GALLERY_ZOOM_MIN, GALLERY_ZOOM_MAX);
    new_width = gallery_zoom_scaled_dimension(g_gallery_runtime.page_width, new_zoom);
    new_height = gallery_zoom_scaled_dimension(g_gallery_runtime.page_height, new_zoom);
    centered_left = (g_gallery_runtime.page_width - new_width) / 2;
    centered_top = (g_gallery_runtime.page_height - new_height) / 2;
    desired_left = focus_x - (content_x_milli * new_zoom) / 1000;
    desired_top = focus_y - (content_y_milli * new_zoom) / 1000;
    g_gallery_runtime.zoom_milli = new_zoom;
    g_gallery_runtime.zoom_pan_x = desired_left - centered_left;
    g_gallery_runtime.zoom_pan_y = desired_top - centered_top;
    gallery_zoom_apply_layout(env, request_layout);
}

static void gallery_zoom_reset(JNIEnv* env, int request_layout) {
    g_gallery_runtime.zoom_milli = GALLERY_ZOOM_MIN;
    g_gallery_runtime.zoom_pan_x = 0;
    g_gallery_runtime.zoom_pan_y = 0;
    g_gallery_runtime.pinching = 0;
    g_gallery_runtime.zoom_panning = 0;
    gallery_zoom_apply_layout(env, request_layout);
}

static void gallery_zoom_cycle(JNIEnv* env) {
    int target;
    int current = g_gallery_runtime.zoom_milli > 0
            ? g_gallery_runtime.zoom_milli : GALLERY_ZOOM_MIN;
    if (current < 1500) target = GALLERY_ZOOM_DOUBLE;
    else if (current < 3000) target = GALLERY_ZOOM_QUAD;
    else if (current < 6000) target = GALLERY_ZOOM_MAX;
    else target = GALLERY_ZOOM_MIN;
    gallery_zoom_set_around(env, target,
            g_gallery_runtime.page_width / 2,
            g_gallery_runtime.page_height / 2, 1);
}

static void gallery_zoom_sync_note(JNIEnv* env, jobject activity) {
    jclass activity_cls;
    jfieldID note_field;
    jobject note = 0;
    int changed = 0;
    activity_cls = (*env)->GetObjectClass(env, activity);
    note_field = activity_cls ? (*env)->GetFieldID(env, activity_cls,
            "note", "Lde/offlineflipnote/legacy/PpmFile;") : 0;
    if (note_field) note = (*env)->GetObjectField(env, activity, note_field);
    if ((*env)->ExceptionCheck(env)) {
        gallery_clear_exception(env);
        note = 0;
    }
    if ((note && !g_gallery_runtime.note_identity)
            || (!note && g_gallery_runtime.note_identity)
            || (note && g_gallery_runtime.note_identity
                && !(*env)->IsSameObject(env, note, g_gallery_runtime.note_identity)))
        changed = 1;
    if (changed) {
        gallery_delete_global(env, &g_gallery_runtime.note_identity);
        if (note) g_gallery_runtime.note_identity = (*env)->NewGlobalRef(env, note);
        gallery_zoom_reset(env, 1);
    }
    if (note) (*env)->DeleteLocalRef(env, note);
    if (activity_cls) (*env)->DeleteLocalRef(env, activity_cls);
    gallery_clear_exception(env);
}

static void gallery_text_append_jstring(
        JNIEnv* env, GalleryText* out, jstring value, const char* fallback) {
    const char* utf = value ? (*env)->GetStringUTFChars(env, value, 0) : 0;
    if (utf && utf[0]) gallery_text_append(out, utf);
    else gallery_text_append(out, fallback ? fallback : "-");
    if (utf && value) (*env)->ReleaseStringUTFChars(env, value, utf);
    gallery_clear_exception(env);
}

static jstring gallery_format_double(
        JNIEnv* env, jclass activity_cls, const char* method_name, jdouble value) {
    jmethodID method = activity_cls ? (*env)->GetStaticMethodID(env, activity_cls,
            method_name, "(D)Ljava/lang/String;") : 0;
    jstring result = method ? (jstring)(*env)->CallStaticObjectMethod(
            env, activity_cls, method, value) : 0;
    if ((*env)->ExceptionCheck(env)) {
        gallery_clear_exception(env);
        result = 0;
    }
    return result;
}

static jstring gallery_format_bytes(JNIEnv* env, jclass activity_cls, jlong value) {
    jmethodID method = activity_cls ? (*env)->GetStaticMethodID(env, activity_cls,
            "formatBytes", "(J)Ljava/lang/String;") : 0;
    jstring result = method ? (jstring)(*env)->CallStaticObjectMethod(
            env, activity_cls, method, value) : 0;
    if ((*env)->ExceptionCheck(env)) {
        gallery_clear_exception(env);
        result = 0;
    }
    return result;
}

static int gallery_build_metadata_text(
        JNIEnv* env, jobject activity, char* buffer, int capacity) {
    GalleryText out;
    jclass activity_cls = 0;
    jfieldID note_field = 0, name_field = 0, player_field = 0;
    jobject note = 0, player = 0;
    jstring name = 0, author = 0, rate_text = 0, bgm_text = 0;
    jstring duration_text = 0, size_text_obj = 0;
    jclass note_cls = 0, player_cls = 0;
    jmethodID get_author = 0, get_frames = 0, get_rate = 0;
    jmethodID get_duration = 0, has_audio = 0, is_loop = 0;
    jmethodID get_bgm_rate = 0, get_shown_frame = 0;
    jfieldID data_field = 0;
    jbyteArray data_array = 0;
    jint frames = 0, shown_frame = -1, byte_count = 0;
    jdouble rate = 0.0, duration = 0.0, bgm_rate = 0.0;
    jboolean audio = JNI_FALSE, loop = JNI_FALSE;

    if (!buffer || capacity <= 0) return 0;
    buffer[0] = 0;
    out.data = buffer; out.length = 0; out.capacity = capacity;

    activity_cls = (*env)->GetObjectClass(env, activity);
    note_field = activity_cls ? (*env)->GetFieldID(env, activity_cls,
            "note", "Lde/offlineflipnote/legacy/PpmFile;") : 0;
    name_field = activity_cls ? (*env)->GetFieldID(env, activity_cls,
            "noteName", "Ljava/lang/String;") : 0;
    player_field = activity_cls ? (*env)->GetFieldID(env, activity_cls,
            "playerView", "Lde/offlineflipnote/legacy/PpmPlayerView;") : 0;
    note = note_field ? (*env)->GetObjectField(env, activity, note_field) : 0;
    name = name_field ? (jstring)(*env)->GetObjectField(env, activity, name_field) : 0;
    player = player_field ? (*env)->GetObjectField(env, activity, player_field) : 0;
    if ((*env)->ExceptionCheck(env) || !note) {
        gallery_clear_exception(env);
        goto done;
    }

    note_cls = (*env)->GetObjectClass(env, note);
    player_cls = player ? (*env)->GetObjectClass(env, player) : 0;
    get_author = note_cls ? (*env)->GetMethodID(env, note_cls,
            "getAuthor", "()Ljava/lang/String;") : 0;
    get_frames = note_cls ? (*env)->GetMethodID(env, note_cls,
            "getFrameCount", "()I") : 0;
    get_rate = note_cls ? (*env)->GetMethodID(env, note_cls,
            "getFrameRate", "()D") : 0;
    get_duration = note_cls ? (*env)->GetMethodID(env, note_cls,
            "getDurationSeconds", "()D") : 0;
    has_audio = note_cls ? (*env)->GetMethodID(env, note_cls,
            "hasAudio", "()Z") : 0;
    is_loop = note_cls ? (*env)->GetMethodID(env, note_cls,
            "isLoop", "()Z") : 0;
    get_bgm_rate = note_cls ? (*env)->GetMethodID(env, note_cls,
            "getBgmFrameRate", "()D") : 0;
    data_field = note_cls ? (*env)->GetFieldID(env, note_cls, "data", "[B") : 0;
    get_shown_frame = player_cls ? (*env)->GetMethodID(env, player_cls,
            "getShownFrame", "()I") : 0;

    if (get_author) author = (jstring)(*env)->CallObjectMethod(env, note, get_author);
    if (get_frames) frames = (*env)->CallIntMethod(env, note, get_frames);
    if (get_rate) rate = (*env)->CallDoubleMethod(env, note, get_rate);
    if (get_duration) duration = (*env)->CallDoubleMethod(env, note, get_duration);
    if (has_audio) audio = (*env)->CallBooleanMethod(env, note, has_audio);
    if (is_loop) loop = (*env)->CallBooleanMethod(env, note, is_loop);
    if (get_bgm_rate) bgm_rate = (*env)->CallDoubleMethod(env, note, get_bgm_rate);
    if (data_field) data_array = (jbyteArray)(*env)->GetObjectField(env, note, data_field);
    if (data_array) byte_count = (*env)->GetArrayLength(env, data_array);
    if (get_shown_frame) shown_frame = (*env)->CallIntMethod(env, player, get_shown_frame);
    if ((*env)->ExceptionCheck(env)) {
        gallery_clear_exception(env);
        goto done;
    }

    rate_text = gallery_format_double(env, activity_cls, "trimRate", rate);
    bgm_text = gallery_format_double(env, activity_cls, "trimRate", bgm_rate);
    duration_text = gallery_format_double(env, activity_cls, "formatTime", duration);
    size_text_obj = gallery_format_bytes(env, activity_cls, (jlong)byte_count);

    gallery_text_append(&out, "Dateiname: ");
    gallery_text_append_jstring(env, &out, name, "-");
    gallery_text_append(&out, "\nAutor: ");
    gallery_text_append_jstring(env, &out, author, "-");
    gallery_text_append(&out, "\nFormat: Flipnote Studio PPM");
    gallery_text_append(&out, "\nAufloesung: 256 x 192");
    gallery_text_append(&out, "\nFrames: ");
    gallery_text_append_int(&out, frames);
    if (shown_frame >= 0 && frames > 0) {
        gallery_text_append(&out, "\nAktueller Frame: ");
        gallery_text_append_int(&out, shown_frame + 1);
        gallery_text_append(&out, " / ");
        gallery_text_append_int(&out, frames);
    }
    gallery_text_append(&out, "\nBildrate: ");
    gallery_text_append_jstring(env, &out, rate_text, "?");
    gallery_text_append(&out, " fps");
    gallery_text_append(&out, "\nDauer: ");
    gallery_text_append_jstring(env, &out, duration_text, "?");
    gallery_text_append(&out, "\nSchleife: ");
    gallery_text_append(&out, loop ? "Ja" : "Nein");
    gallery_text_append(&out, "\nAudio: ");
    gallery_text_append(&out, audio ? "Ja" : "Nein");
    if (audio) {
        gallery_text_append(&out, "\nBGM-Bildrate: ");
        gallery_text_append_jstring(env, &out, bgm_text, "?");
        gallery_text_append(&out, " fps");
    }
    gallery_text_append(&out, "\nDateigroesse: ");
    gallery_text_append_jstring(env, &out, size_text_obj, "?");

 done:
    if (size_text_obj) (*env)->DeleteLocalRef(env, size_text_obj);
    if (duration_text) (*env)->DeleteLocalRef(env, duration_text);
    if (bgm_text) (*env)->DeleteLocalRef(env, bgm_text);
    if (rate_text) (*env)->DeleteLocalRef(env, rate_text);
    if (data_array) (*env)->DeleteLocalRef(env, data_array);
    if (author) (*env)->DeleteLocalRef(env, author);
    if (player_cls) (*env)->DeleteLocalRef(env, player_cls);
    if (note_cls) (*env)->DeleteLocalRef(env, note_cls);
    if (player) (*env)->DeleteLocalRef(env, player);
    if (name) (*env)->DeleteLocalRef(env, name);
    if (note) (*env)->DeleteLocalRef(env, note);
    if (activity_cls) (*env)->DeleteLocalRef(env, activity_cls);
    gallery_clear_exception(env);
    return out.length > 0;
}

static void gallery_show_metadata_dialog(JNIEnv* env, jobject activity) {
    char message_buffer[1400];
    jclass builder_cls = 0;
    jmethodID ctor = 0, set_title = 0, set_message = 0;
    jmethodID set_positive = 0, show = 0;
    jobject builder = 0, dialog = 0;
    jstring title = 0, message = 0, ok = 0;
    if (!gallery_build_metadata_text(env, activity,
            message_buffer, (int)sizeof(message_buffer))) return;
    builder_cls = (*env)->FindClass(env, "android/app/AlertDialog$Builder");
    ctor = builder_cls ? (*env)->GetMethodID(env, builder_cls, "<init>",
            "(Landroid/content/Context;)V") : 0;
    set_title = builder_cls ? (*env)->GetMethodID(env, builder_cls, "setTitle",
            "(Ljava/lang/CharSequence;)Landroid/app/AlertDialog$Builder;") : 0;
    set_message = builder_cls ? (*env)->GetMethodID(env, builder_cls, "setMessage",
            "(Ljava/lang/CharSequence;)Landroid/app/AlertDialog$Builder;") : 0;
    set_positive = builder_cls ? (*env)->GetMethodID(env, builder_cls,
            "setPositiveButton", "(Ljava/lang/CharSequence;Landroid/content/DialogInterface$OnClickListener;)Landroid/app/AlertDialog$Builder;") : 0;
    show = builder_cls ? (*env)->GetMethodID(env, builder_cls, "show",
            "()Landroid/app/AlertDialog;") : 0;
    title = (*env)->NewStringUTF(env, "Flipnote-Info");
    message = (*env)->NewStringUTF(env, message_buffer);
    ok = (*env)->NewStringUTF(env, "OK");
    if (!builder_cls || !ctor || !set_title || !set_message || !set_positive
            || !show || !title || !message || !ok) {
        gallery_clear_exception(env);
        goto done;
    }
    builder = (*env)->NewObject(env, builder_cls, ctor, activity);
    if (!builder || (*env)->ExceptionCheck(env)) {
        gallery_clear_exception(env);
        goto done;
    }
    (*env)->CallObjectMethod(env, builder, set_title, title);
    (*env)->CallObjectMethod(env, builder, set_message, message);
    (*env)->CallObjectMethod(env, builder, set_positive, ok, 0);
    dialog = (*env)->CallObjectMethod(env, builder, show);
    gallery_clear_exception(env);
 done:
    if (dialog) (*env)->DeleteLocalRef(env, dialog);
    if (builder) (*env)->DeleteLocalRef(env, builder);
    if (ok) (*env)->DeleteLocalRef(env, ok);
    if (message) (*env)->DeleteLocalRef(env, message);
    if (title) (*env)->DeleteLocalRef(env, title);
    if (builder_cls) (*env)->DeleteLocalRef(env, builder_cls);
}

static int gallery_jstring_equals(JNIEnv* env, jstring value, const char* expected) {
    const char* utf;
    int equal = 0;
    if (!value || !expected) return 0;
    utf = (*env)->GetStringUTFChars(env, value, 0);
    if (utf) equal = strcmp(utf, expected) == 0;
    if (utf) (*env)->ReleaseStringUTFChars(env, value, utf);
    gallery_clear_exception(env);
    return equal;
}

static void gallery_handle_external_intent_once(JNIEnv* env, jobject activity) {
    jclass activity_cls = 0, intent_cls = 0;
    jmethodID get_intent = 0, get_action = 0, get_data = 0, get_extra = 0;
    jmethodID load_uri = 0;
    jobject intent = 0, uri = 0;
    jstring action = 0, stream_key = 0;
    int is_view = 0, is_send = 0;

    if (g_gallery_runtime.external_intent_checked) return;
    g_gallery_runtime.external_intent_checked = 1;
    activity_cls = (*env)->GetObjectClass(env, activity);
    get_intent = activity_cls ? (*env)->GetMethodID(env, activity_cls,
            "getIntent", "()Landroid/content/Intent;") : 0;
    load_uri = activity_cls ? (*env)->GetMethodID(env, activity_cls,
            "loadSelectedUri", "(Landroid/net/Uri;)V") : 0;
    if (!get_intent || !load_uri) { gallery_clear_exception(env); goto done; }
    intent = (*env)->CallObjectMethod(env, activity, get_intent);
    if (!intent || (*env)->ExceptionCheck(env)) { gallery_clear_exception(env); goto done; }
    intent_cls = (*env)->GetObjectClass(env, intent);
    get_action = intent_cls ? (*env)->GetMethodID(env, intent_cls,
            "getAction", "()Ljava/lang/String;") : 0;
    get_data = intent_cls ? (*env)->GetMethodID(env, intent_cls,
            "getData", "()Landroid/net/Uri;") : 0;
    get_extra = intent_cls ? (*env)->GetMethodID(env, intent_cls,
            "getParcelableExtra", "(Ljava/lang/String;)Landroid/os/Parcelable;") : 0;
    action = get_action ? (jstring)(*env)->CallObjectMethod(env, intent, get_action) : 0;
    if ((*env)->ExceptionCheck(env)) { gallery_clear_exception(env); goto done; }
    is_view = gallery_jstring_equals(env, action, "android.intent.action.VIEW");
    is_send = gallery_jstring_equals(env, action, "android.intent.action.SEND");
    if (is_view && get_data)
        uri = (*env)->CallObjectMethod(env, intent, get_data);
    else if (is_send && get_extra) {
        stream_key = (*env)->NewStringUTF(env, "android.intent.extra.STREAM");
        if (stream_key)
            uri = (*env)->CallObjectMethod(env, intent, get_extra, stream_key);
    }
    if ((*env)->ExceptionCheck(env)) { gallery_clear_exception(env); uri = 0; }
    if (uri) {
        gallery_clear_selection();
        (*env)->CallVoidMethod(env, activity, load_uri, uri);
        gallery_clear_exception(env);
    }
 done:
    if (stream_key) (*env)->DeleteLocalRef(env, stream_key);
    if (uri) (*env)->DeleteLocalRef(env, uri);
    if (action) (*env)->DeleteLocalRef(env, action);
    if (intent_cls) (*env)->DeleteLocalRef(env, intent_cls);
    if (intent) (*env)->DeleteLocalRef(env, intent);
    if (activity_cls) (*env)->DeleteLocalRef(env, activity_cls);
    gallery_clear_exception(env);
}

static int gallery_copy_selection_if_changed(void) {
    int changed = 0;
    int i;
    gallery_selection_lock();
    if (g_gallery_runtime.generation != g_gallery_selection.generation) {
        g_gallery_runtime.generation = g_gallery_selection.generation;
        g_gallery_runtime.count = g_gallery_selection.count;
        g_gallery_runtime.current = g_gallery_selection.current;
        g_gallery_runtime.from_zip = g_gallery_selection.from_zip;
        copy_text(g_gallery_runtime.zip_path, g_gallery_selection.zip_path, MAX_PATH);
        copy_text(g_gallery_runtime.virtual_directory,
                  g_gallery_selection.virtual_directory, MAX_PATH);
        for (i = 0; i < g_gallery_selection.count; i++) {
            copy_text(g_gallery_runtime.paths[i], g_gallery_selection.paths[i], MAX_PATH);
            g_gallery_runtime.locators[i] = g_gallery_selection.locators[i];
        }
        changed = 1;
    }
    gallery_selection_unlock();
    return changed;
}

static int gallery_read_file(const char* path, unsigned char** out_data, int* out_size) {
    FILE* file;
    int size = file_size32(path);
    unsigned char* data;
    if (size < PPM_MIN_BYTES || size > PPM_MAX_PREVIEW_BYTES) return 0;
    data = (unsigned char*)malloc((size_t)size);
    if (!data) return 0;
    file = fopen(path, "rb");
    if (!file) { free(data); return 0; }
    if (fread(data, 1, (size_t)size, file) != (size_t)size) {
        fclose(file); free(data); return 0;
    }
    fclose(file);
    *out_data = data;
    *out_size = size;
    return 1;
}

static int gallery_metadata(const unsigned char* data, int size,
                            int* frame_count, int* interval_ms) {
    static const int frame_intervals[9] = {
        2000, 2000, 1000, 500, 250, 167, 83, 50, 33
    };
    uint32_t animation_size;
    int frames;
    int sound_header;
    int speed_index = 5;
    if (size < PPM_MIN_BYTES || data[0] != 'P' || data[1] != 'A'
            || data[2] != 'R' || data[3] != 'A'
            || !read_u32le_at(data, size, 4, &animation_size)
            || !read_u16le_at(data, size, 12, &frames)) return 0;
    frames++;
    if (frames < 1 || frames > 999 || animation_size > 0x7fffffffu) return 0;
    if ((int)animation_size <= 0x7fffffff - PPM_ANIMATION_OFFSET - frames - 3) {
        sound_header = (PPM_ANIMATION_OFFSET + (int)animation_size + frames + 3) & ~3;
        if (range_ok(size, sound_header, 18)) {
            int candidate = 8 - data[sound_header + 16];
            if (candidate >= 0 && candidate < 9) speed_index = candidate;
        }
    }
    *frame_count = frames;
    *interval_ms = frame_intervals[speed_index];
    return 1;
}

static jobject gallery_parse_note(JNIEnv* env, const char* path,
                                  int* frame_count, int* interval_ms) {
    unsigned char* data = 0;
    int size = 0;
    jbyteArray bytes = 0;
    jclass ppm_cls = 0;
    jmethodID parse = 0;
    jobject note = 0;
    if (!gallery_read_file(path, &data, &size)) return 0;
    if (!gallery_metadata(data, size, frame_count, interval_ms)) {
        free(data);
        return 0;
    }
    bytes = (*env)->NewByteArray(env, size);
    if (!bytes) { gallery_clear_exception(env); free(data); return 0; }
    (*env)->SetByteArrayRegion(env, bytes, 0, size, (const jbyte*)data);
    free(data);
    if ((*env)->ExceptionCheck(env)) {
        gallery_clear_exception(env);
        (*env)->DeleteLocalRef(env, bytes);
        return 0;
    }
    ppm_cls = (*env)->FindClass(env, "de/offlineflipnote/legacy/PpmFile");
    parse = ppm_cls ? (*env)->GetStaticMethodID(env, ppm_cls, "parse",
            "([B)Lde/offlineflipnote/legacy/PpmFile;") : 0;
    if (parse) note = (*env)->CallStaticObjectMethod(env, ppm_cls, parse, bytes);
    if ((*env)->ExceptionCheck(env)) {
        gallery_clear_exception(env);
        note = 0;
    }
    if (ppm_cls) (*env)->DeleteLocalRef(env, ppm_cls);
    (*env)->DeleteLocalRef(env, bytes);
    return note;
}

static int gallery_current_stretched(JNIEnv* env) {
    jclass view_cls;
    jmethodID method;
    jboolean result = 0;
    if (!g_gallery_runtime.current_view) return 0;
    view_cls = (*env)->GetObjectClass(env, g_gallery_runtime.current_view);
    method = view_cls ? (*env)->GetMethodID(env, view_cls, "isStretched", "()Z") : 0;
    if (method) result = (*env)->CallBooleanMethod(env,
            g_gallery_runtime.current_view, method);
    gallery_clear_exception(env);
    if (view_cls) (*env)->DeleteLocalRef(env, view_cls);
    return result ? 1 : 0;
}

static void gallery_set_button_visibility(
        JNIEnv* env, jobject button, int visible, int* remembered) {
    jclass button_cls;
    jmethodID set_visibility;
    if (!button || !remembered || visible == *remembered) return;
    button_cls = (*env)->GetObjectClass(env, button);
    set_visibility = button_cls ? (*env)->GetMethodID(env, button_cls,
            "setVisibility", "(I)V") : 0;
    if (set_visibility)
        (*env)->CallVoidMethod(env, button, set_visibility, visible ? 0 : 8);
    gallery_clear_exception(env);
    if (button_cls) (*env)->DeleteLocalRef(env, button_cls);
    *remembered = visible;
}

static void gallery_update_overlay_buttons(JNIEnv* env, jobject activity) {
    jclass activity_cls;
    jfieldID controls_field, note_field;
    jboolean controls_visible = JNI_FALSE;
    jobject note = 0;
    int back_visible;
    int zoom_visible;
    int info_visible;
    activity_cls = (*env)->GetObjectClass(env, activity);
    controls_field = activity_cls ? (*env)->GetFieldID(env, activity_cls,
            "controlsVisible", "Z") : 0;
    note_field = activity_cls ? (*env)->GetFieldID(env, activity_cls,
            "note", "Lde/offlineflipnote/legacy/PpmFile;") : 0;
    if (controls_field)
        controls_visible = (*env)->GetBooleanField(env, activity, controls_field);
    if (note_field) note = (*env)->GetObjectField(env, activity, note_field);
    gallery_clear_exception(env);
    back_visible = g_gallery_runtime.list_matches_note && controls_visible;
    zoom_visible = note && controls_visible;
    info_visible = note && controls_visible;
    gallery_set_button_visibility(env, g_gallery_runtime.back_button,
                                  back_visible, &g_gallery_runtime.back_visible);
    gallery_set_button_visibility(env, g_gallery_runtime.zoom_button,
                                  zoom_visible, &g_gallery_runtime.zoom_visible);
    gallery_set_button_visibility(env, g_gallery_runtime.info_button,
                                  info_visible, &g_gallery_runtime.info_visible);
    gallery_zoom_update_button_label(env);
    if (note) (*env)->DeleteLocalRef(env, note);
    if (activity_cls) (*env)->DeleteLocalRef(env, activity_cls);
}


/* Keep the Java seek bar measurable and visible after the player view is
   reparented into the gallery pager. On recent Android versions an early
   reparent during the first layout pass can leave this one sibling at
   GONE/zero height even though the rest of the control menu is visible. The
   original SeekBar and its Java listener are retained; this only restores its
   visibility and the original 24-dp layout height. */
static void gallery_repair_seekbar(JNIEnv* env, jobject activity) {
    jclass activity_cls = 0;
    jfieldID controls_field = 0;
    jfieldID root_field = 0;
    jboolean controls_visible = JNI_FALSE;
    jobject root = 0;
    jclass bar_cls = 0;
    jclass root_cls = 0;
    jobject layout_params = 0;
    jclass lp_cls = 0;
    jmethodID get_visibility = 0;
    jmethodID set_visibility = 0;
    jmethodID get_height = 0;
    jmethodID get_lp = 0;
    jmethodID set_lp = 0;
    jmethodID request_layout = 0;
    jmethodID invalidate = 0;
    jmethodID root_request_layout = 0;
    jmethodID dp_method = 0;
    jfieldID lp_height_field = 0;
    jint visibility = 0;
    jint measured_height = 0;
    jint layout_height = 0;
    jint desired_height = 24;
    int changed = 0;

    if (!g_gallery_runtime.seek_bar) return;
    activity_cls = (*env)->GetObjectClass(env, activity);
    controls_field = activity_cls ? (*env)->GetFieldID(env, activity_cls,
            "controlsVisible", "Z") : 0;
    root_field = activity_cls ? (*env)->GetFieldID(env, activity_cls,
            "rootLayout", "Landroid/widget/LinearLayout;") : 0;
    dp_method = activity_cls ? (*env)->GetMethodID(env, activity_cls,
            "dp", "(I)I") : 0;
    if (controls_field)
        controls_visible = (*env)->GetBooleanField(env, activity, controls_field);
    if (root_field) root = (*env)->GetObjectField(env, activity, root_field);
    if (dp_method)
        desired_height = (*env)->CallIntMethod(env, activity, dp_method, 24);
    if ((*env)->ExceptionCheck(env)) {
        gallery_clear_exception(env);
        desired_height = 24;
    }
    if (!controls_visible) goto done;

    bar_cls = (*env)->GetObjectClass(env, g_gallery_runtime.seek_bar);
    get_visibility = bar_cls ? (*env)->GetMethodID(env, bar_cls,
            "getVisibility", "()I") : 0;
    set_visibility = bar_cls ? (*env)->GetMethodID(env, bar_cls,
            "setVisibility", "(I)V") : 0;
    get_height = bar_cls ? (*env)->GetMethodID(env, bar_cls,
            "getHeight", "()I") : 0;
    get_lp = bar_cls ? (*env)->GetMethodID(env, bar_cls,
            "getLayoutParams", "()Landroid/view/ViewGroup$LayoutParams;") : 0;
    set_lp = bar_cls ? (*env)->GetMethodID(env, bar_cls,
            "setLayoutParams", "(Landroid/view/ViewGroup$LayoutParams;)V") : 0;
    request_layout = bar_cls ? (*env)->GetMethodID(env, bar_cls,
            "requestLayout", "()V") : 0;
    invalidate = bar_cls ? (*env)->GetMethodID(env, bar_cls,
            "invalidate", "()V") : 0;
    if (get_visibility)
        visibility = (*env)->CallIntMethod(env, g_gallery_runtime.seek_bar,
                                           get_visibility);
    if (get_height)
        measured_height = (*env)->CallIntMethod(env, g_gallery_runtime.seek_bar,
                                                get_height);
    if (get_lp)
        layout_params = (*env)->CallObjectMethod(env, g_gallery_runtime.seek_bar,
                                                 get_lp);
    if ((*env)->ExceptionCheck(env)) {
        gallery_clear_exception(env);
        goto done;
    }

    if (visibility != 0 && set_visibility) {
        (*env)->CallVoidMethod(env, g_gallery_runtime.seek_bar,
                               set_visibility, 0); /* VISIBLE */
        changed = 1;
    }
    if (layout_params) {
        lp_cls = (*env)->FindClass(env, "android/view/ViewGroup$LayoutParams");
        lp_height_field = lp_cls ? (*env)->GetFieldID(env, lp_cls,
                "height", "I") : 0;
        if (lp_height_field) {
            layout_height = (*env)->GetIntField(env, layout_params,
                                                lp_height_field);
            if (layout_height <= 0) {
                (*env)->SetIntField(env, layout_params, lp_height_field,
                                    desired_height > 0 ? desired_height : 24);
                if (set_lp)
                    (*env)->CallVoidMethod(env, g_gallery_runtime.seek_bar,
                                           set_lp, layout_params);
                changed = 1;
            }
        }
    }
    if (measured_height <= 0) changed = 1;
    if (changed) {
        if (request_layout)
            (*env)->CallVoidMethod(env, g_gallery_runtime.seek_bar,
                                   request_layout);
        if (invalidate)
            (*env)->CallVoidMethod(env, g_gallery_runtime.seek_bar, invalidate);
        if (root) {
            root_cls = (*env)->GetObjectClass(env, root);
            root_request_layout = root_cls ? (*env)->GetMethodID(env, root_cls,
                    "requestLayout", "()V") : 0;
            if (root_request_layout)
                (*env)->CallVoidMethod(env, root, root_request_layout);
        }
    }
    gallery_clear_exception(env);

done:
    if (root_cls) (*env)->DeleteLocalRef(env, root_cls);
    if (lp_cls) (*env)->DeleteLocalRef(env, lp_cls);
    if (layout_params) (*env)->DeleteLocalRef(env, layout_params);
    if (bar_cls) (*env)->DeleteLocalRef(env, bar_cls);
    if (root) (*env)->DeleteLocalRef(env, root);
    if (activity_cls) (*env)->DeleteLocalRef(env, activity_cls);
    gallery_clear_exception(env);
}

static void gallery_page_set_stretched(JNIEnv* env, GalleryPage* page, int stretched) {
    jclass view_cls;
    jmethodID method;
    if (!page->view) return;
    view_cls = (*env)->GetObjectClass(env, page->view);
    method = view_cls ? (*env)->GetMethodID(env, view_cls, "setStretched", "(Z)V") : 0;
    if (method) (*env)->CallVoidMethod(env, page->view, method,
                                      stretched ? JNI_TRUE : JNI_FALSE);
    gallery_clear_exception(env);
    if (view_cls) (*env)->DeleteLocalRef(env, view_cls);
}

static int gallery_page_load(JNIEnv* env, GalleryPage* page,
                             int index, int stretched) {
    jobject note;
    jobject note_global;
    jclass view_cls;
    jmethodID set_note;
    int frames = 0;
    int interval = 0;
    gallery_page_set_empty(env, page);
    if (!gallery_materialize(index)) return 0;
    note = gallery_parse_note(env, g_gallery_runtime.paths[index], &frames, &interval);
    if (!note) return 0;
    note_global = (*env)->NewGlobalRef(env, note);
    if (!note_global) {
        gallery_clear_exception(env);
        (*env)->DeleteLocalRef(env, note);
        return 0;
    }
    view_cls = (*env)->GetObjectClass(env, page->view);
    set_note = view_cls ? (*env)->GetMethodID(env, view_cls, "setNote",
            "(Lde/offlineflipnote/legacy/PpmFile;)V") : 0;
    if (!set_note) {
        gallery_clear_exception(env);
        (*env)->DeleteGlobalRef(env, note_global);
        if (view_cls) (*env)->DeleteLocalRef(env, view_cls);
        (*env)->DeleteLocalRef(env, note);
        return 0;
    }
    (*env)->CallVoidMethod(env, page->view, set_note, note);
    if ((*env)->ExceptionCheck(env)) {
        gallery_clear_exception(env);
        (*env)->DeleteGlobalRef(env, note_global);
        if (view_cls) (*env)->DeleteLocalRef(env, view_cls);
        (*env)->DeleteLocalRef(env, note);
        return 0;
    }
    if (view_cls) (*env)->DeleteLocalRef(env, view_cls);
    (*env)->DeleteLocalRef(env, note);
    page->note = note_global;
    page->valid = 1;
    page->frame_count = frames;
    page->frame_interval_ms = interval;
    page->frame = 0;
    page->next_due_ms = monotonicish_millis() + (uint32_t)interval;
    gallery_page_set_stretched(env, page, stretched);
    return 1;
}

static void gallery_page_advance(JNIEnv* env, GalleryPage* page, uint32_t now) {
    int steps = 0;
    jclass view_cls;
    jmethodID show_frame;
    if (!page->valid || !page->view || page->frame_count <= 1) return;
    if ((int32_t)(now - page->next_due_ms) > 1000)
        page->next_due_ms = now;
    view_cls = (*env)->GetObjectClass(env, page->view);
    show_frame = view_cls ? (*env)->GetMethodID(env, view_cls, "showFrame", "(I)V") : 0;
    if (!show_frame) {
        gallery_clear_exception(env);
        if (view_cls) (*env)->DeleteLocalRef(env, view_cls);
        page->valid = 0;
        return;
    }
    while (time_reached(now, page->next_due_ms) && steps < MAX_CATCHUP_FRAMES) {
        page->frame++;
        if (page->frame >= page->frame_count) page->frame = 0;
        (*env)->CallVoidMethod(env, page->view, show_frame, page->frame);
        if ((*env)->ExceptionCheck(env)) {
            gallery_clear_exception(env);
            page->valid = 0;
            break;
        }
        page->next_due_ms += (uint32_t)page->frame_interval_ms;
        steps++;
    }
    if (steps >= MAX_CATCHUP_FRAMES && time_reached(now, page->next_due_ms))
        page->next_due_ms = now + (uint32_t)page->frame_interval_ms;
    (*env)->DeleteLocalRef(env, view_cls);
}

static void gallery_scroll_to(JNIEnv* env, int x) {
    jclass cls;
    jmethodID method;
    if (!g_gallery_runtime.pager) return;
    cls = (*env)->GetObjectClass(env, g_gallery_runtime.pager);
    method = cls ? (*env)->GetMethodID(env, cls, "scrollTo", "(II)V") : 0;
    if (method) (*env)->CallVoidMethod(env, g_gallery_runtime.pager, method, x, 0);
    gallery_clear_exception(env);
    if (cls) (*env)->DeleteLocalRef(env, cls);
}

static int gallery_scroll_x(JNIEnv* env) {
    jclass cls;
    jmethodID method;
    jint value = 0;
    if (!g_gallery_runtime.pager) return 0;
    cls = (*env)->GetObjectClass(env, g_gallery_runtime.pager);
    method = cls ? (*env)->GetMethodID(env, cls, "getScrollX", "()I") : 0;
    if (method) value = (*env)->CallIntMethod(env, g_gallery_runtime.pager, method);
    gallery_clear_exception(env);
    if (cls) (*env)->DeleteLocalRef(env, cls);
    return value;
}

static void gallery_set_view_width(JNIEnv* env, jobject view, int width) {
    jclass lp_cls;
    jclass view_cls;
    jmethodID ctor;
    jmethodID set_lp;
    jobject lp;
    if (!view || width <= 0) return;
    lp_cls = (*env)->FindClass(env, "android/widget/LinearLayout$LayoutParams");
    view_cls = (*env)->GetObjectClass(env, view);
    ctor = lp_cls ? (*env)->GetMethodID(env, lp_cls, "<init>", "(II)V") : 0;
    set_lp = view_cls ? (*env)->GetMethodID(env, view_cls, "setLayoutParams",
            "(Landroid/view/ViewGroup$LayoutParams;)V") : 0;
    lp = (ctor && set_lp) ? (*env)->NewObject(env, lp_cls, ctor, width, -1) : 0;
    if (lp) (*env)->CallVoidMethod(env, view, set_lp, lp);
    gallery_clear_exception(env);
    if (lp) (*env)->DeleteLocalRef(env, lp);
    if (view_cls) (*env)->DeleteLocalRef(env, view_cls);
    if (lp_cls) (*env)->DeleteLocalRef(env, lp_cls);
}

static int gallery_update_page_width(JNIEnv* env) {
    jclass pager_cls = 0;
    jclass page_cls = 0;
    jmethodID get_width = 0;
    jmethodID get_height = 0;
    jint width = 0;
    jint height = 0;
    int width_changed;
    int height_changed;
    if (!g_gallery_runtime.pager || !g_gallery_runtime.current_page) return 0;
    pager_cls = (*env)->GetObjectClass(env, g_gallery_runtime.pager);
    page_cls = (*env)->GetObjectClass(env, g_gallery_runtime.current_page);
    get_width = pager_cls ? (*env)->GetMethodID(env, pager_cls, "getWidth", "()I") : 0;
    get_height = page_cls ? (*env)->GetMethodID(env, page_cls, "getHeight", "()I") : 0;
    if (get_width) width = (*env)->CallIntMethod(env, g_gallery_runtime.pager, get_width);
    if (get_height) height = (*env)->CallIntMethod(env, g_gallery_runtime.current_page, get_height);
    gallery_clear_exception(env);
    if (page_cls) (*env)->DeleteLocalRef(env, page_cls);
    if (pager_cls) (*env)->DeleteLocalRef(env, pager_cls);
    if (width <= 0) return 0;
    if (height <= 0) height = g_gallery_runtime.page_height;
    width_changed = width != g_gallery_runtime.page_width;
    height_changed = height > 0 && height != g_gallery_runtime.page_height;
    if (width_changed) {
        gallery_set_view_width(env, g_gallery_runtime.previous.view, width);
        gallery_set_view_width(env, g_gallery_runtime.current_page, width);
        gallery_set_view_width(env, g_gallery_runtime.next.view, width);
        g_gallery_runtime.page_width = width;
    }
    if (height > 0) g_gallery_runtime.page_height = height;
    if (width_changed || height_changed) {
        g_gallery_runtime.touching = 0;
        g_gallery_runtime.pinching = 0;
        g_gallery_runtime.zoom_panning = 0;
        g_gallery_runtime.settling = 0;
        if (g_gallery_runtime.zoom_milli > GALLERY_ZOOM_MIN)
            gallery_zoom_reset(env, 1);
        else
            gallery_zoom_apply_layout(env, 1);
        /* setLayoutParams() may schedule another layout pass. Keep retrying
           the centre scroll until HorizontalScrollView confirms it. */
        g_gallery_runtime.center_pending = 1;
        gallery_scroll_to(env, width);
    }
    return width;
}

static void gallery_finish_pending_center(JNIEnv* env, int width) {
    if (!g_gallery_runtime.center_pending || width <= 0
            || g_gallery_runtime.touching || g_gallery_runtime.settling)
        return;
    gallery_scroll_to(env, width);
    if (gallery_scroll_x(env) == width)
        g_gallery_runtime.center_pending = 0;
}

static int gallery_attach(JNIEnv* env, jobject activity) {
    jclass main_cls = 0;
    jfieldID player_field;
    jfieldID seek_field;
    jobject current = 0;
    jobject seek = 0;
    jclass current_cls = 0;
    jclass seek_cls = 0;
    jmethodID get_width;
    jmethodID get_height;
    jmethodID seek_get_height;
    jmethodID seek_get_visibility;
    jmethodID get_parent;
    jmethodID get_lp;
    jmethodID dp_method;
    jint width = 0;
    jint height = 0;
    jint seek_height = 0;
    jint seek_visibility = 0;
    jobject parent = 0;
    jobject old_lp = 0;
    jclass group_cls = 0;
    jmethodID index_of;
    jmethodID remove_view;
    jmethodID add_at;
    jint child_index = -1;
    jclass container_cls = 0, pager_cls = 0, strip_cls = 0;
    jclass ppm_view_cls = 0, button_cls = 0;
    jclass linear_lp_cls = 0, frame_lp_cls = 0;
    jmethodID container_ctor, pager_ctor, strip_ctor, ppm_ctor, button_ctor;
    jmethodID set_orientation, linear_lp_ctor, frame_lp_ctor, frame_lp_gravity_ctor;
    jobject container = 0, pager = 0, strip = 0, current_page = 0;
    jobject prev = 0, next = 0;
    jobject back = 0, zoom = 0, info = 0;
    jobject prev_lp = 0, cur_lp = 0, next_lp = 0, strip_lp = 0;
    jobject current_child_lp = 0;
    jobject pager_lp = 0, back_lp = 0, zoom_lp = 0, info_lp = 0;
    jmethodID strip_add, strip_remove, pager_add, container_add, container_remove;
    jstring back_text = 0, zoom_text = 0, info_text = 0;
    int back_w = 96, back_h = 42, back_margin = 6, back_pad = 4;
    int zoom_w = 64, info_w = 64;
    int removed = 0;
    int current_in_page = 0;
    int success = 0;

    if (g_gallery_runtime.activity
            && (*env)->IsSameObject(env, g_gallery_runtime.activity, activity))
        return g_gallery_runtime.attached;
    if (g_gallery_runtime.activity) gallery_runtime_release(env);

    main_cls = (*env)->GetObjectClass(env, activity);
    player_field = main_cls ? (*env)->GetFieldID(env, main_cls, "playerView",
            "Lde/offlineflipnote/legacy/PpmPlayerView;") : 0;
    seek_field = main_cls ? (*env)->GetFieldID(env, main_cls, "seekBar",
            "Landroid/widget/SeekBar;") : 0;
    if (!player_field || !seek_field) { gallery_clear_exception(env); goto done; }
    current = (*env)->GetObjectField(env, activity, player_field);
    seek = (*env)->GetObjectField(env, activity, seek_field);
    if (!current || !seek) goto done;
    current_cls = (*env)->GetObjectClass(env, current);
    seek_cls = (*env)->GetObjectClass(env, seek);
    get_width = current_cls ? (*env)->GetMethodID(env, current_cls, "getWidth", "()I") : 0;
    get_height = current_cls ? (*env)->GetMethodID(env, current_cls, "getHeight", "()I") : 0;
    seek_get_height = seek_cls ? (*env)->GetMethodID(env, seek_cls, "getHeight", "()I") : 0;
    seek_get_visibility = seek_cls ? (*env)->GetMethodID(env, seek_cls,
            "getVisibility", "()I") : 0;
    get_parent = current_cls ? (*env)->GetMethodID(env, current_cls, "getParent",
            "()Landroid/view/ViewParent;") : 0;
    get_lp = current_cls ? (*env)->GetMethodID(env, current_cls, "getLayoutParams",
            "()Landroid/view/ViewGroup$LayoutParams;") : 0;
    if (!get_width || !get_height || !seek_get_height || !seek_get_visibility
            || !get_parent || !get_lp) {
        gallery_clear_exception(env); goto done;
    }
    width = (*env)->CallIntMethod(env, current, get_width);
    height = (*env)->CallIntMethod(env, current, get_height);
    seek_height = (*env)->CallIntMethod(env, seek, seek_get_height);
    seek_visibility = (*env)->CallIntMethod(env, seek, seek_get_visibility);
    parent = (*env)->CallObjectMethod(env, current, get_parent);
    old_lp = (*env)->CallObjectMethod(env, current, get_lp);
    /* Wait for the original interface, including the seek bar, to finish its
       first layout pass before reparenting the player view. */
    if ((*env)->ExceptionCheck(env) || width <= 0 || height <= 0
            || (seek_visibility == 0 && seek_height <= 0)
            || !parent || !old_lp) {
        gallery_clear_exception(env); goto done;
    }

    group_cls = (*env)->GetObjectClass(env, parent);
    index_of = group_cls ? (*env)->GetMethodID(env, group_cls, "indexOfChild",
            "(Landroid/view/View;)I") : 0;
    remove_view = group_cls ? (*env)->GetMethodID(env, group_cls, "removeView",
            "(Landroid/view/View;)V") : 0;
    add_at = group_cls ? (*env)->GetMethodID(env, group_cls, "addView",
            "(Landroid/view/View;ILandroid/view/ViewGroup$LayoutParams;)V") : 0;
    if (!index_of || !remove_view || !add_at) { gallery_clear_exception(env); goto done; }
    child_index = (*env)->CallIntMethod(env, parent, index_of, current);
    if ((*env)->ExceptionCheck(env) || child_index < 0) { gallery_clear_exception(env); goto done; }

    container_cls = (*env)->FindClass(env, "android/widget/FrameLayout");
    pager_cls = (*env)->FindClass(env, "android/widget/HorizontalScrollView");
    strip_cls = (*env)->FindClass(env, "android/widget/LinearLayout");
    ppm_view_cls = (*env)->FindClass(env, "de/offlineflipnote/legacy/PpmPlayerView");
    button_cls = (*env)->FindClass(env, "android/widget/Button");
    linear_lp_cls = (*env)->FindClass(env, "android/widget/LinearLayout$LayoutParams");
    frame_lp_cls = (*env)->FindClass(env, "android/widget/FrameLayout$LayoutParams");
    container_ctor = container_cls ? (*env)->GetMethodID(env, container_cls, "<init>",
            "(Landroid/content/Context;)V") : 0;
    pager_ctor = pager_cls ? (*env)->GetMethodID(env, pager_cls, "<init>",
            "(Landroid/content/Context;)V") : 0;
    strip_ctor = strip_cls ? (*env)->GetMethodID(env, strip_cls, "<init>",
            "(Landroid/content/Context;)V") : 0;
    ppm_ctor = ppm_view_cls ? (*env)->GetMethodID(env, ppm_view_cls, "<init>",
            "(Landroid/content/Context;)V") : 0;
    button_ctor = button_cls ? (*env)->GetMethodID(env, button_cls, "<init>",
            "(Landroid/content/Context;)V") : 0;
    set_orientation = strip_cls ? (*env)->GetMethodID(env, strip_cls, "setOrientation", "(I)V") : 0;
    linear_lp_ctor = linear_lp_cls ? (*env)->GetMethodID(env, linear_lp_cls,
            "<init>", "(II)V") : 0;
    frame_lp_ctor = frame_lp_cls ? (*env)->GetMethodID(env, frame_lp_cls,
            "<init>", "(II)V") : 0;
    frame_lp_gravity_ctor = frame_lp_cls ? (*env)->GetMethodID(env, frame_lp_cls,
            "<init>", "(III)V") : 0;
    if (!container_ctor || !pager_ctor || !strip_ctor || !ppm_ctor || !button_ctor
            || !set_orientation || !linear_lp_ctor || !frame_lp_ctor
            || !frame_lp_gravity_ctor) {
        gallery_clear_exception(env); goto done;
    }

    dp_method = main_cls ? (*env)->GetMethodID(env, main_cls, "dp", "(I)I") : 0;
    if (dp_method) {
        back_w = (*env)->CallIntMethod(env, activity, dp_method, 96);
        back_h = (*env)->CallIntMethod(env, activity, dp_method, 42);
        back_margin = (*env)->CallIntMethod(env, activity, dp_method, 6);
        back_pad = (*env)->CallIntMethod(env, activity, dp_method, 4);
        zoom_w = (*env)->CallIntMethod(env, activity, dp_method, 64);
        info_w = (*env)->CallIntMethod(env, activity, dp_method, 64);
        if ((*env)->ExceptionCheck(env)) {
            gallery_clear_exception(env);
            back_w = 96; back_h = 42; back_margin = 6; back_pad = 4;
            zoom_w = 64; info_w = 64;
        }
    } else {
        gallery_clear_exception(env);
    }

    container = (*env)->NewObject(env, container_cls, container_ctor, activity);
    pager = (*env)->NewObject(env, pager_cls, pager_ctor, activity);
    strip = (*env)->NewObject(env, strip_cls, strip_ctor, activity);
    current_page = (*env)->NewObject(env, container_cls, container_ctor, activity);
    prev = (*env)->NewObject(env, ppm_view_cls, ppm_ctor, activity);
    next = (*env)->NewObject(env, ppm_view_cls, ppm_ctor, activity);
    back = (*env)->NewObject(env, button_cls, button_ctor, activity);
    zoom = (*env)->NewObject(env, button_cls, button_ctor, activity);
    info = (*env)->NewObject(env, button_cls, button_ctor, activity);
    if (!container || !pager || !strip || !current_page || !prev || !next
            || !back || !zoom || !info || (*env)->ExceptionCheck(env)) {
        gallery_clear_exception(env); goto done;
    }
    (*env)->CallVoidMethod(env, strip, set_orientation, 0);

    prev_lp = (*env)->NewObject(env, linear_lp_cls, linear_lp_ctor, width, -1);
    cur_lp = (*env)->NewObject(env, linear_lp_cls, linear_lp_ctor, width, -1);
    next_lp = (*env)->NewObject(env, linear_lp_cls, linear_lp_ctor, width, -1);
    current_child_lp = (*env)->NewObject(env, frame_lp_cls, frame_lp_ctor, -1, -1);
    strip_lp = (*env)->NewObject(env, frame_lp_cls, frame_lp_ctor, -2, -1);
    pager_lp = (*env)->NewObject(env, frame_lp_cls, frame_lp_ctor, -1, -1);
    back_lp = (*env)->NewObject(env, frame_lp_cls, frame_lp_gravity_ctor,
                                back_w, back_h, 51); /* TOP | LEFT */
    zoom_lp = (*env)->NewObject(env, frame_lp_cls, frame_lp_gravity_ctor,
                                zoom_w, back_h, 49); /* TOP | CENTER_HORIZONTAL */
    info_lp = (*env)->NewObject(env, frame_lp_cls, frame_lp_gravity_ctor,
                                info_w, back_h, 53); /* TOP | RIGHT */
    strip_add = (*env)->GetMethodID(env, strip_cls, "addView",
            "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V");
    strip_remove = (*env)->GetMethodID(env, strip_cls, "removeView",
            "(Landroid/view/View;)V");
    pager_add = (*env)->GetMethodID(env, pager_cls, "addView",
            "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V");
    container_add = (*env)->GetMethodID(env, container_cls, "addView",
            "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V");
    container_remove = (*env)->GetMethodID(env, container_cls, "removeView",
            "(Landroid/view/View;)V");
    if (!prev_lp || !cur_lp || !next_lp || !current_child_lp || !strip_lp
            || !pager_lp || !back_lp || !zoom_lp || !info_lp
            || !strip_add || !strip_remove || !pager_add || !container_add
            || !container_remove) {
        gallery_clear_exception(env); goto done;
    }

    {
        jmethodID margins = (*env)->GetMethodID(env, frame_lp_cls, "setMargins", "(IIII)V");
        jmethodID set_text = (*env)->GetMethodID(env, button_cls, "setText",
                "(Ljava/lang/CharSequence;)V");
        jmethodID set_padding = (*env)->GetMethodID(env, button_cls, "setPadding", "(IIII)V");
        jmethodID set_visibility = (*env)->GetMethodID(env, button_cls, "setVisibility", "(I)V");
        jmethodID set_listener = (*env)->GetMethodID(env, button_cls,
                "setOnTouchListener", "(Landroid/view/View$OnTouchListener;)V");
        back_text = (*env)->NewStringUTF(env, "< Zurueck");
        zoom_text = (*env)->NewStringUTF(env, "1x");
        info_text = (*env)->NewStringUTF(env, "Info");
        if (margins) {
            (*env)->CallVoidMethod(env, back_lp, margins,
                                   back_margin, back_margin, 0, 0);
            (*env)->CallVoidMethod(env, zoom_lp, margins,
                                   0, back_margin, 0, 0);
            (*env)->CallVoidMethod(env, info_lp, margins,
                                   0, back_margin, back_margin, 0);
        }
        if (set_text && back_text) (*env)->CallVoidMethod(env, back, set_text, back_text);
        if (set_text && zoom_text) (*env)->CallVoidMethod(env, zoom, set_text, zoom_text);
        if (set_text && info_text) (*env)->CallVoidMethod(env, info, set_text, info_text);
        if (set_padding) {
            (*env)->CallVoidMethod(env, back, set_padding, back_pad, 0, back_pad, 0);
            (*env)->CallVoidMethod(env, zoom, set_padding, back_pad, 0, back_pad, 0);
            (*env)->CallVoidMethod(env, info, set_padding, back_pad, 0, back_pad, 0);
        }
        if (set_visibility) {
            (*env)->CallVoidMethod(env, back, set_visibility, 8); /* GONE */
            (*env)->CallVoidMethod(env, zoom, set_visibility, 8); /* GONE */
            (*env)->CallVoidMethod(env, info, set_visibility, 8); /* GONE */
        }
        if (set_listener) {
            (*env)->CallVoidMethod(env, back, set_listener, activity);
            (*env)->CallVoidMethod(env, zoom, set_listener, activity);
            (*env)->CallVoidMethod(env, info, set_listener, activity);
        }
        if ((*env)->ExceptionCheck(env)) { gallery_clear_exception(env); goto done; }
    }

    (*env)->CallVoidMethod(env, parent, remove_view, current);
    if ((*env)->ExceptionCheck(env)) { gallery_clear_exception(env); goto done; }
    removed = 1;
    (*env)->CallVoidMethod(env, current_page, container_add, current, current_child_lp);
    current_in_page = 1;
    (*env)->CallVoidMethod(env, strip, strip_add, prev, prev_lp);
    (*env)->CallVoidMethod(env, strip, strip_add, current_page, cur_lp);
    (*env)->CallVoidMethod(env, strip, strip_add, next, next_lp);
    (*env)->CallVoidMethod(env, pager, pager_add, strip, strip_lp);
    (*env)->CallVoidMethod(env, container, container_add, pager, pager_lp);
    (*env)->CallVoidMethod(env, container, container_add, back, back_lp);
    (*env)->CallVoidMethod(env, container, container_add, zoom, zoom_lp);
    (*env)->CallVoidMethod(env, container, container_add, info, info_lp);
    (*env)->CallVoidMethod(env, parent, add_at, container, child_index, old_lp);
    if ((*env)->ExceptionCheck(env)) { gallery_clear_exception(env); goto done; }
    removed = 0;

    {
        jmethodID fill = (*env)->GetMethodID(env, pager_cls, "setFillViewport", "(Z)V");
        jmethodID bars = (*env)->GetMethodID(env, pager_cls,
                "setHorizontalScrollBarEnabled", "(Z)V");
        jmethodID overscroll = (*env)->GetMethodID(env, pager_cls, "setOverScrollMode", "(I)V");
        jmethodID scroll = (*env)->GetMethodID(env, pager_cls, "scrollTo", "(II)V");
        jmethodID listener = current_cls ? (*env)->GetMethodID(env, current_cls,
                "setOnTouchListener", "(Landroid/view/View$OnTouchListener;)V") : 0;
        if (fill) (*env)->CallVoidMethod(env, pager, fill, JNI_TRUE);
        if (bars) (*env)->CallVoidMethod(env, pager, bars, JNI_FALSE);
        if (overscroll) (*env)->CallVoidMethod(env, pager, overscroll, 2);
        if (scroll) (*env)->CallVoidMethod(env, pager, scroll, width, 0);
        if (listener) (*env)->CallVoidMethod(env, current, listener, activity);
        if ((*env)->ExceptionCheck(env)) { gallery_clear_exception(env); goto done; }
    }

    g_gallery_runtime.activity = (*env)->NewGlobalRef(env, activity);
    g_gallery_runtime.container = (*env)->NewGlobalRef(env, container);
    g_gallery_runtime.pager = (*env)->NewGlobalRef(env, pager);
    g_gallery_runtime.strip = (*env)->NewGlobalRef(env, strip);
    g_gallery_runtime.current_page = (*env)->NewGlobalRef(env, current_page);
    g_gallery_runtime.current_view = (*env)->NewGlobalRef(env, current);
    g_gallery_runtime.current_layout = (*env)->NewGlobalRef(env, current_child_lp);
    g_gallery_runtime.back_button = (*env)->NewGlobalRef(env, back);
    g_gallery_runtime.zoom_button = (*env)->NewGlobalRef(env, zoom);
    g_gallery_runtime.info_button = (*env)->NewGlobalRef(env, info);
    g_gallery_runtime.seek_bar = (*env)->NewGlobalRef(env, seek);
    g_gallery_runtime.previous.view = (*env)->NewGlobalRef(env, prev);
    g_gallery_runtime.next.view = (*env)->NewGlobalRef(env, next);
    if (!g_gallery_runtime.activity || !g_gallery_runtime.container
            || !g_gallery_runtime.pager || !g_gallery_runtime.strip
            || !g_gallery_runtime.current_page || !g_gallery_runtime.current_view
            || !g_gallery_runtime.current_layout || !g_gallery_runtime.back_button
            || !g_gallery_runtime.zoom_button || !g_gallery_runtime.info_button
            || !g_gallery_runtime.seek_bar
            || !g_gallery_runtime.previous.view || !g_gallery_runtime.next.view) {
        gallery_clear_exception(env);
        gallery_runtime_release(env);
        goto done;
    }
    g_gallery_runtime.attached = 1;
    g_gallery_runtime.page_width = width;
    g_gallery_runtime.page_height = height;
    g_gallery_runtime.zoom_milli = GALLERY_ZOOM_MIN;
    g_gallery_runtime.zoom_label_milli = 0;
    g_gallery_runtime.last_stretched = -1;
    g_gallery_runtime.back_visible = 0;
    g_gallery_runtime.zoom_visible = 0;
    g_gallery_runtime.info_visible = 0;
    gallery_zoom_apply_layout(env, 1);
    /* The first scrollTo() above can be clamped to zero because the newly
       inserted HorizontalScrollView has not completed layout yet. A later UI
       tick must centre the real player page after measurement. */
    g_gallery_runtime.center_pending = 1;
    success = 1;

done:
    /* Restore the original child if construction failed after removeView(). */
    if (!success && removed && parent && current && old_lp && add_at) {
        if (current_in_page && current_page && container_remove) {
            (*env)->CallVoidMethod(env, current_page, container_remove, current);
            gallery_clear_exception(env);
        }
        (*env)->CallVoidMethod(env, parent, add_at, current, child_index, old_lp);
        gallery_clear_exception(env);
    }
    if (info_text) (*env)->DeleteLocalRef(env, info_text);
    if (zoom_text) (*env)->DeleteLocalRef(env, zoom_text);
    if (back_text) (*env)->DeleteLocalRef(env, back_text);
    if (info_lp) (*env)->DeleteLocalRef(env, info_lp);
    if (zoom_lp) (*env)->DeleteLocalRef(env, zoom_lp);
    if (back_lp) (*env)->DeleteLocalRef(env, back_lp);
    if (pager_lp) (*env)->DeleteLocalRef(env, pager_lp);
    if (strip_lp) (*env)->DeleteLocalRef(env, strip_lp);
    if (current_child_lp) (*env)->DeleteLocalRef(env, current_child_lp);
    if (next_lp) (*env)->DeleteLocalRef(env, next_lp);
    if (cur_lp) (*env)->DeleteLocalRef(env, cur_lp);
    if (prev_lp) (*env)->DeleteLocalRef(env, prev_lp);
    if (info) (*env)->DeleteLocalRef(env, info);
    if (zoom) (*env)->DeleteLocalRef(env, zoom);
    if (back) (*env)->DeleteLocalRef(env, back);
    if (next) (*env)->DeleteLocalRef(env, next);
    if (prev) (*env)->DeleteLocalRef(env, prev);
    if (current_page) (*env)->DeleteLocalRef(env, current_page);
    if (strip) (*env)->DeleteLocalRef(env, strip);
    if (pager) (*env)->DeleteLocalRef(env, pager);
    if (container) (*env)->DeleteLocalRef(env, container);
    if (frame_lp_cls) (*env)->DeleteLocalRef(env, frame_lp_cls);
    if (linear_lp_cls) (*env)->DeleteLocalRef(env, linear_lp_cls);
    if (button_cls) (*env)->DeleteLocalRef(env, button_cls);
    if (ppm_view_cls) (*env)->DeleteLocalRef(env, ppm_view_cls);
    if (strip_cls) (*env)->DeleteLocalRef(env, strip_cls);
    if (pager_cls) (*env)->DeleteLocalRef(env, pager_cls);
    if (container_cls) (*env)->DeleteLocalRef(env, container_cls);
    if (group_cls) (*env)->DeleteLocalRef(env, group_cls);
    if (old_lp) (*env)->DeleteLocalRef(env, old_lp);
    if (parent) (*env)->DeleteLocalRef(env, parent);
    if (seek_cls) (*env)->DeleteLocalRef(env, seek_cls);
    if (current_cls) (*env)->DeleteLocalRef(env, current_cls);
    if (seek) (*env)->DeleteLocalRef(env, seek);
    if (current) (*env)->DeleteLocalRef(env, current);
    if (main_cls) (*env)->DeleteLocalRef(env, main_cls);
    return success;
}

static int gallery_find_current_from_note_name(JNIEnv* env, jobject activity) {
    jclass cls;
    jfieldID field;
    jstring name_obj;
    const char* name;
    int found = -1;
    int i;
    cls = (*env)->GetObjectClass(env, activity);
    field = cls ? (*env)->GetFieldID(env, cls, "noteName", "Ljava/lang/String;") : 0;
    name_obj = field ? (jstring)(*env)->GetObjectField(env, activity, field) : 0;
    if ((*env)->ExceptionCheck(env)) { gallery_clear_exception(env); name_obj = 0; }
    name = name_obj ? (*env)->GetStringUTFChars(env, name_obj, 0) : 0;
    if (name) {
        for (i = 0; i < g_gallery_runtime.count; i++) {
            if (strcmp(name, gallery_basename(g_gallery_runtime.paths[i])) == 0) {
                found = i;
                break;
            }
        }
        (*env)->ReleaseStringUTFChars(env, name_obj, name);
    }
    if (name_obj) (*env)->DeleteLocalRef(env, name_obj);
    if (cls) (*env)->DeleteLocalRef(env, cls);
    return found;
}

static void gallery_refresh_neighbors(JNIEnv* env) {
    int stretched = gallery_current_stretched(env);
    if (g_gallery_runtime.zoom_milli > GALLERY_ZOOM_MIN)
        gallery_zoom_reset(env, 1);
    gallery_page_set_empty(env, &g_gallery_runtime.previous);
    gallery_page_set_empty(env, &g_gallery_runtime.next);
    if (g_gallery_runtime.list_matches_note) {
        if (g_gallery_runtime.current > 0)
            gallery_page_load(env, &g_gallery_runtime.previous,
                              g_gallery_runtime.current - 1, stretched);
        if (g_gallery_runtime.current + 1 < g_gallery_runtime.count)
            gallery_page_load(env, &g_gallery_runtime.next,
                              g_gallery_runtime.current + 1, stretched);
    }
    g_gallery_runtime.last_stretched = stretched;
    g_gallery_runtime.touching = 0;
    g_gallery_runtime.settling = 0;
    g_gallery_runtime.center_pending = 1;
    if (g_gallery_runtime.page_width > 0)
        gallery_scroll_to(env, g_gallery_runtime.page_width);
}

static int gallery_load_main_file(JNIEnv* env, jobject activity, int index) {
    jclass cls;
    jfieldID playing_field;
    jmethodID load_file;
    jmethodID start;
    jclass file_cls;
    jmethodID file_ctor;
    jstring path_obj;
    jobject file_obj;
    jboolean was_playing = JNI_FALSE;
    int old_index = g_gallery_runtime.current;
    if (index < 0 || index >= g_gallery_runtime.count) return 0;
    /* A ZIP-backed page must exist as a file before the player opens it. */
    if (!gallery_materialize(index)) return 0;
    cls = (*env)->GetObjectClass(env, activity);
    playing_field = cls ? (*env)->GetFieldID(env, cls, "playing", "Z") : 0;
    load_file = cls ? (*env)->GetMethodID(env, cls, "loadSelectedFile",
            "(Ljava/io/File;)V") : 0;
    start = cls ? (*env)->GetMethodID(env, cls, "startPlayback", "()V") : 0;
    if (!load_file) { gallery_clear_exception(env); if (cls) (*env)->DeleteLocalRef(env, cls); return 0; }
    if (playing_field) was_playing = (*env)->GetBooleanField(env, activity, playing_field);
    file_cls = (*env)->FindClass(env, "java/io/File");
    file_ctor = file_cls ? (*env)->GetMethodID(env, file_cls, "<init>",
            "(Ljava/lang/String;)V") : 0;
    path_obj = (*env)->NewStringUTF(env, g_gallery_runtime.paths[index]);
    file_obj = (file_cls && file_ctor && path_obj)
        ? (*env)->NewObject(env, file_cls, file_ctor, path_obj) : 0;
    if (!file_obj || (*env)->ExceptionCheck(env)) {
        gallery_clear_exception(env);
        if (file_obj) (*env)->DeleteLocalRef(env, file_obj);
        if (path_obj) (*env)->DeleteLocalRef(env, path_obj);
        if (file_cls) (*env)->DeleteLocalRef(env, file_cls);
        if (cls) (*env)->DeleteLocalRef(env, cls);
        return 0;
    }
    (*env)->CallVoidMethod(env, activity, load_file, file_obj);
    if ((*env)->ExceptionCheck(env)) {
        gallery_clear_exception(env);
        g_gallery_runtime.current = old_index;
    } else {
        g_gallery_runtime.current = index;
        g_gallery_runtime.list_matches_note = 1;
        gallery_refresh_neighbors(env);
        if (was_playing && start) (*env)->CallVoidMethod(env, activity, start);
        gallery_clear_exception(env);
    }
    (*env)->DeleteLocalRef(env, file_obj);
    (*env)->DeleteLocalRef(env, path_obj);
    if (file_cls) (*env)->DeleteLocalRef(env, file_cls);
    if (cls) (*env)->DeleteLocalRef(env, cls);
    return g_gallery_runtime.current == index;
}

static void gallery_start_settle(JNIEnv* env, int target, int direction) {
    g_gallery_runtime.settling = 1;
    g_gallery_runtime.settle_from = gallery_scroll_x(env);
    g_gallery_runtime.settle_to = target;
    g_gallery_runtime.settle_direction = direction;
    g_gallery_runtime.settle_start_ms = monotonicish_millis();
    g_gallery_runtime.touching = 0;
}

static void gallery_tick_settle(JNIEnv* env, jobject activity, uint32_t now) {
    uint32_t elapsed;
    int progress;
    int inverse;
    int eased;
    int x;
    int direction;
    if (!g_gallery_runtime.settling) return;
    elapsed = now - g_gallery_runtime.settle_start_ms;
    if (elapsed >= GALLERY_SETTLE_MS) {
        gallery_scroll_to(env, g_gallery_runtime.settle_to);
        direction = g_gallery_runtime.settle_direction;
        g_gallery_runtime.settling = 0;
        if (direction != 0)
            gallery_load_main_file(env, activity,
                    g_gallery_runtime.current + direction);
        else if (g_gallery_runtime.page_width > 0)
            gallery_scroll_to(env, g_gallery_runtime.page_width);
        return;
    }
    progress = (int)((elapsed * 1024u) / GALLERY_SETTLE_MS);
    inverse = 1024 - progress;
    eased = 1024 - (inverse * inverse) / 1024;
    x = g_gallery_runtime.settle_from
      + ((g_gallery_runtime.settle_to - g_gallery_runtime.settle_from) * eased) / 1024;
    gallery_scroll_to(env, x);
}

static void gallery_disallow_parent_intercept(JNIEnv* env) {
    jclass cls;
    jmethodID method;
    if (!g_gallery_runtime.pager) return;
    cls = (*env)->GetObjectClass(env, g_gallery_runtime.pager);
    method = cls ? (*env)->GetMethodID(env, cls, "requestDisallowInterceptTouchEvent", "(Z)V") : 0;
    if (method) (*env)->CallVoidMethod(env, g_gallery_runtime.pager, method, JNI_TRUE);
    gallery_clear_exception(env);
    if (cls) (*env)->DeleteLocalRef(env, cls);
}

static void gallery_perform_click(JNIEnv* env, jobject view) {
    jclass cls = (*env)->GetObjectClass(env, view);
    jmethodID method = cls ? (*env)->GetMethodID(env, cls, "performClick", "()Z") : 0;
    if (method) (*env)->CallBooleanMethod(env, view, method);
    gallery_clear_exception(env);
    if (cls) (*env)->DeleteLocalRef(env, cls);
}

static int gallery_launch_browser(JNIEnv* env, jobject activity) {
    jclass intent_cls = 0;
    jclass activity_cls = 0;
    jmethodID ctor = 0;
    jmethodID set_class_name = 0;
    jmethodID add_category = 0;
    jmethodID set_type = 0;
    jmethodID add_flags = 0;
    jmethodID put_extra = 0;
    jmethodID start_for_result = 0;
    jstring action = 0;
    jstring package_name = 0;
    jstring class_name = 0;
    jstring category = 0;
    jstring type = 0;
    jstring directory_key = 0;
    jstring path_key = 0;
    jstring directory_value = 0;
    jstring path_value = 0;
    jobject intent = 0;
    char return_directory[MAX_PATH];
    char return_path[MAX_PATH];
    int success = 0;

    if (!gallery_prepare_browser_return_request(return_directory, return_path)) return 0;

    intent_cls = (*env)->FindClass(env, "android/content/Intent");
    activity_cls = (*env)->GetObjectClass(env, activity);
    ctor = intent_cls ? (*env)->GetMethodID(env, intent_cls, "<init>",
            "(Ljava/lang/String;)V") : 0;
    set_class_name = intent_cls ? (*env)->GetMethodID(env, intent_cls, "setClassName",
            "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;") : 0;
    add_category = intent_cls ? (*env)->GetMethodID(env, intent_cls, "addCategory",
            "(Ljava/lang/String;)Landroid/content/Intent;") : 0;
    set_type = intent_cls ? (*env)->GetMethodID(env, intent_cls, "setType",
            "(Ljava/lang/String;)Landroid/content/Intent;") : 0;
    add_flags = intent_cls ? (*env)->GetMethodID(env, intent_cls, "addFlags",
            "(I)Landroid/content/Intent;") : 0;
    put_extra = intent_cls ? (*env)->GetMethodID(env, intent_cls, "putExtra",
            "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;") : 0;
    start_for_result = activity_cls ? (*env)->GetMethodID(env, activity_cls,
            "startActivityForResult", "(Landroid/content/Intent;I)V") : 0;
    if (!ctor || !set_class_name || !add_category || !set_type
            || !put_extra || !start_for_result) {
        gallery_clear_exception(env);
        goto done;
    }

    action = (*env)->NewStringUTF(env, "android.intent.action.GET_CONTENT");
    package_name = (*env)->NewStringUTF(env, "de.offlineflipnote.legacy");
    class_name = (*env)->NewStringUTF(env, "de.offlineflipnote.legacy.BrowserActivity");
    category = (*env)->NewStringUTF(env, "android.intent.category.OPENABLE");
    type = (*env)->NewStringUTF(env, "*/*");
    directory_key = (*env)->NewStringUTF(env, "flipnote.browser.return.directory");
    path_key = (*env)->NewStringUTF(env, "flipnote.browser.return.path");
    directory_value = (*env)->NewStringUTF(env, return_directory);
    path_value = (*env)->NewStringUTF(env, return_path);
    if (!action || !package_name || !class_name || !category || !type
            || !directory_key || !path_key || !directory_value || !path_value) {
        gallery_clear_exception(env);
        goto done;
    }

    intent = (*env)->NewObject(env, intent_cls, ctor, action);
    if (!intent) { gallery_clear_exception(env); goto done; }
    (*env)->CallObjectMethod(env, intent, set_class_name, package_name, class_name);
    (*env)->CallObjectMethod(env, intent, add_category, category);
    (*env)->CallObjectMethod(env, intent, set_type, type);
    (*env)->CallObjectMethod(env, intent, put_extra, directory_key, directory_value);
    (*env)->CallObjectMethod(env, intent, put_extra, path_key, path_value);
    if (add_flags) (*env)->CallObjectMethod(env, intent, add_flags, 1);
    (*env)->CallVoidMethod(env, activity, start_for_result, intent, 42);
    if ((*env)->ExceptionCheck(env)) {
        gallery_clear_exception(env);
        goto done;
    }
    success = 1;

done:
    if (!success) gallery_cancel_browser_return_request();
    if (intent) (*env)->DeleteLocalRef(env, intent);
    if (path_value) (*env)->DeleteLocalRef(env, path_value);
    if (directory_value) (*env)->DeleteLocalRef(env, directory_value);
    if (path_key) (*env)->DeleteLocalRef(env, path_key);
    if (directory_key) (*env)->DeleteLocalRef(env, directory_key);
    if (type) (*env)->DeleteLocalRef(env, type);
    if (category) (*env)->DeleteLocalRef(env, category);
    if (class_name) (*env)->DeleteLocalRef(env, class_name);
    if (package_name) (*env)->DeleteLocalRef(env, package_name);
    if (action) (*env)->DeleteLocalRef(env, action);
    if (activity_cls) (*env)->DeleteLocalRef(env, activity_cls);
    if (intent_cls) (*env)->DeleteLocalRef(env, intent_cls);
    return success;
}

/* Android's system back button returns to the originating thumbnail folder
   whenever the current Flipnote belongs to an active browser selection. */
__attribute__((visibility("default")))
void Java_de_offlineflipnote_legacy_MainActivity_onBackPressed(
        JNIEnv* env, jobject activity) {
    jclass base_cls;
    jmethodID base_back;
    if (gallery_launch_browser(env, activity)) return;
    base_cls = (*env)->FindClass(env, "android/app/Activity");
    base_back = base_cls ? (*env)->GetMethodID(env, base_cls,
            "onBackPressed", "()V") : 0;
    if (base_back)
        (*env)->CallNonvirtualVoidMethod(env, activity, base_cls, base_back);
    gallery_clear_exception(env);
    if (base_cls) (*env)->DeleteLocalRef(env, base_cls);
}

/* Called from MainActivity's existing UI ticker after updatePlaybackUi(). */
__attribute__((visibility("default")))
void Java_de_offlineflipnote_legacy_MainActivity_galleryTick(JNIEnv* env, jobject activity) {
    int selection_changed;
    int found = -1;
    int match_changed = 0;
    int width;
    int stretched;
    uint32_t now;

    if (g_gallery_runtime.activity
            && !(*env)->IsSameObject(env, g_gallery_runtime.activity, activity))
        gallery_runtime_release(env);

    gallery_maintain_immersive(env, activity);
    if (!g_gallery_runtime.attached && !gallery_attach(env, activity)) return;
    gallery_handle_external_intent_once(env, activity);
    selection_changed = gallery_copy_selection_if_changed();
    width = gallery_update_page_width(env);
    if (width <= 0) return;
    gallery_zoom_sync_note(env, activity);

    if (g_gallery_runtime.count > 0)
        found = gallery_find_current_from_note_name(env, activity);
    if (found >= 0) {
        if (!g_gallery_runtime.list_matches_note || found != g_gallery_runtime.current) {
            g_gallery_runtime.list_matches_note = 1;
            g_gallery_runtime.current = found;
            match_changed = 1;
        }
    } else if (g_gallery_runtime.list_matches_note) {
        g_gallery_runtime.list_matches_note = 0;
        match_changed = 1;
    }
    if (selection_changed || match_changed)
        gallery_refresh_neighbors(env);
    gallery_update_overlay_buttons(env, activity);
    gallery_repair_seekbar(env, activity);
    gallery_finish_pending_center(env, width);

    stretched = gallery_current_stretched(env);
    if (stretched != g_gallery_runtime.last_stretched) {
        if (g_gallery_runtime.last_stretched >= 0
                && g_gallery_runtime.zoom_milli > GALLERY_ZOOM_MIN)
            gallery_zoom_reset(env, 1);
        gallery_page_set_stretched(env, &g_gallery_runtime.previous, stretched);
        gallery_page_set_stretched(env, &g_gallery_runtime.next, stretched);
        g_gallery_runtime.last_stretched = stretched;
    }

    now = monotonicish_millis();
    gallery_tick_settle(env, activity, now);
    gallery_page_advance(env, &g_gallery_runtime.previous, now);
    gallery_page_advance(env, &g_gallery_runtime.next, now);
}

/* MainActivity implements View.OnTouchListener in the patched DEX. */
__attribute__((visibility("default")))
jboolean Java_de_offlineflipnote_legacy_MainActivity_onTouch(
        JNIEnv* env, jobject activity, jobject view, jobject event) {
    jclass event_cls;
    jmethodID get_action;
    jmethodID get_pointer_count;
    jmethodID get_x;
    jmethodID get_y;
    int action;
    int pointer_count = 1;
    int x = 0;
    int y = 0;
    int x1 = 0;
    int y1 = 0;
    int dx;
    int dy;
    int abs_x;
    int abs_y;
    int threshold;
    int direction = 0;
    int has_gallery;
    uint32_t now;

    if (!g_gallery_runtime.attached) return JNI_FALSE;
    event_cls = (*env)->GetObjectClass(env, event);
    get_action = event_cls ? (*env)->GetMethodID(env, event_cls, "getAction", "()I") : 0;
    if (!get_action) {
        gallery_clear_exception(env);
        if (event_cls) (*env)->DeleteLocalRef(env, event_cls);
        return JNI_FALSE;
    }
    action = (*env)->CallIntMethod(env, event, get_action) & AMOTION_EVENT_ACTION_MASK;
    if ((*env)->ExceptionCheck(env)) {
        gallery_clear_exception(env);
        (*env)->DeleteLocalRef(env, event_cls);
        return JNI_FALSE;
    }

    /* The three overlay buttons share MainActivity's native touch listener. */
    if (g_gallery_runtime.back_button
            && (*env)->IsSameObject(env, g_gallery_runtime.back_button, view)) {
        jclass view_cls = (*env)->GetObjectClass(env, view);
        jmethodID set_pressed = view_cls ? (*env)->GetMethodID(env, view_cls,
                "setPressed", "(Z)V") : 0;
        if (set_pressed) {
            if (action == AMOTION_EVENT_ACTION_DOWN)
                (*env)->CallVoidMethod(env, view, set_pressed, JNI_TRUE);
            else if (action == AMOTION_EVENT_ACTION_UP
                    || action == AMOTION_EVENT_ACTION_CANCEL)
                (*env)->CallVoidMethod(env, view, set_pressed, JNI_FALSE);
        }
        gallery_clear_exception(env);
        if (view_cls) (*env)->DeleteLocalRef(env, view_cls);
        (*env)->DeleteLocalRef(env, event_cls);
        if (action == AMOTION_EVENT_ACTION_UP)
            gallery_launch_browser(env, activity);
        return JNI_TRUE;
    }

    if (g_gallery_runtime.zoom_button
            && (*env)->IsSameObject(env, g_gallery_runtime.zoom_button, view)) {
        jclass view_cls = (*env)->GetObjectClass(env, view);
        jmethodID set_pressed = view_cls ? (*env)->GetMethodID(env, view_cls,
                "setPressed", "(Z)V") : 0;
        if (set_pressed) {
            if (action == AMOTION_EVENT_ACTION_DOWN)
                (*env)->CallVoidMethod(env, view, set_pressed, JNI_TRUE);
            else if (action == AMOTION_EVENT_ACTION_UP
                    || action == AMOTION_EVENT_ACTION_CANCEL)
                (*env)->CallVoidMethod(env, view, set_pressed, JNI_FALSE);
        }
        gallery_clear_exception(env);
        if (view_cls) (*env)->DeleteLocalRef(env, view_cls);
        (*env)->DeleteLocalRef(env, event_cls);
        if (action == AMOTION_EVENT_ACTION_UP)
            gallery_zoom_cycle(env);
        return JNI_TRUE;
    }

    if (g_gallery_runtime.info_button
            && (*env)->IsSameObject(env, g_gallery_runtime.info_button, view)) {
        jclass view_cls = (*env)->GetObjectClass(env, view);
        jmethodID set_pressed = view_cls ? (*env)->GetMethodID(env, view_cls,
                "setPressed", "(Z)V") : 0;
        if (set_pressed) {
            if (action == AMOTION_EVENT_ACTION_DOWN)
                (*env)->CallVoidMethod(env, view, set_pressed, JNI_TRUE);
            else if (action == AMOTION_EVENT_ACTION_UP
                    || action == AMOTION_EVENT_ACTION_CANCEL)
                (*env)->CallVoidMethod(env, view, set_pressed, JNI_FALSE);
        }
        gallery_clear_exception(env);
        if (view_cls) (*env)->DeleteLocalRef(env, view_cls);
        (*env)->DeleteLocalRef(env, event_cls);
        if (action == AMOTION_EVENT_ACTION_UP)
            gallery_show_metadata_dialog(env, activity);
        return JNI_TRUE;
    }

    if (!g_gallery_runtime.current_view
            || !(*env)->IsSameObject(env, g_gallery_runtime.current_view, view)) {
        (*env)->DeleteLocalRef(env, event_cls);
        return JNI_FALSE;
    }
    if (g_gallery_runtime.settling) {
        (*env)->DeleteLocalRef(env, event_cls);
        return JNI_TRUE;
    }
    if (gallery_update_page_width(env) <= 0) {
        (*env)->DeleteLocalRef(env, event_cls);
        return JNI_FALSE;
    }

    get_pointer_count = (*env)->GetMethodID(env, event_cls, "getPointerCount", "()I");
    get_x = (*env)->GetMethodID(env, event_cls, "getX", "(I)F");
    get_y = (*env)->GetMethodID(env, event_cls, "getY", "(I)F");
    if (!get_pointer_count || !get_x || !get_y) {
        gallery_clear_exception(env);
        (*env)->DeleteLocalRef(env, event_cls);
        return JNI_FALSE;
    }
    pointer_count = (*env)->CallIntMethod(env, event, get_pointer_count);
    if (pointer_count < 1) pointer_count = 1;
    x = float_bits_to_int((*env)->CallFloatMethod(env, event, get_x, (jint)0))
            + g_gallery_runtime.zoom_left;
    y = float_bits_to_int((*env)->CallFloatMethod(env, event, get_y, (jint)0))
            + g_gallery_runtime.zoom_top;
    if (pointer_count >= 2) {
        x1 = float_bits_to_int((*env)->CallFloatMethod(env, event, get_x, (jint)1))
                + g_gallery_runtime.zoom_left;
        y1 = float_bits_to_int((*env)->CallFloatMethod(env, event, get_y, (jint)1))
                + g_gallery_runtime.zoom_top;
    }
    if ((*env)->ExceptionCheck(env)) {
        gallery_clear_exception(env);
        (*env)->DeleteLocalRef(env, event_cls);
        return JNI_FALSE;
    }
    (*env)->DeleteLocalRef(env, event_cls);
    now = monotonicish_millis();
    has_gallery = g_gallery_runtime.list_matches_note
            && g_gallery_runtime.count > 0;

    if (action == AMOTION_EVENT_ACTION_DOWN) {
        g_gallery_runtime.touching = 1;
        g_gallery_runtime.pinching = 0;
        g_gallery_runtime.horizontal_drag = 0;
        g_gallery_runtime.zoom_panning = 0;
        g_gallery_runtime.touch_down_x = x;
        g_gallery_runtime.touch_down_y = y;
        g_gallery_runtime.pan_start_x = g_gallery_runtime.zoom_pan_x;
        g_gallery_runtime.pan_start_y = g_gallery_runtime.zoom_pan_y;
        g_gallery_runtime.drag_x = 0;
        g_gallery_runtime.touch_down_ms = now;
        gallery_disallow_parent_intercept(env);
        if (g_gallery_runtime.zoom_milli <= GALLERY_ZOOM_MIN)
            gallery_scroll_to(env, g_gallery_runtime.page_width);
        return JNI_TRUE;
    }

    if (action == AMOTION_EVENT_ACTION_POINTER_DOWN && pointer_count >= 2) {
        int distance = gallery_zoom_distance(x, y, x1, y1);
        int focus_x = (x + x1) / 2;
        int focus_y = (y + y1) / 2;
        if (distance > GALLERY_ZOOM_SLOP) {
            gallery_zoom_clamp_pan();
            g_gallery_runtime.touching = 1;
            g_gallery_runtime.pinching = 1;
            g_gallery_runtime.horizontal_drag = 0;
            g_gallery_runtime.zoom_panning = 0;
            g_gallery_runtime.pinch_start_distance = distance;
            g_gallery_runtime.pinch_start_zoom = g_gallery_runtime.zoom_milli;
            g_gallery_runtime.pinch_content_x_milli =
                    ((focus_x - g_gallery_runtime.zoom_left) * 1000)
                    / g_gallery_runtime.zoom_milli;
            g_gallery_runtime.pinch_content_y_milli =
                    ((focus_y - g_gallery_runtime.zoom_top) * 1000)
                    / g_gallery_runtime.zoom_milli;
        }
        return JNI_TRUE;
    }

    if (action == AMOTION_EVENT_ACTION_POINTER_UP) {
        if (g_gallery_runtime.pinching) {
            g_gallery_runtime.pinching = 0;
            g_gallery_runtime.touching = 0;
            g_gallery_runtime.zoom_panning = 0;
            gallery_zoom_apply_layout(env, 1);
        }
        return JNI_TRUE;
    }

    if (!g_gallery_runtime.touching) return JNI_TRUE;

    dx = x - g_gallery_runtime.touch_down_x;
    dy = y - g_gallery_runtime.touch_down_y;
    abs_x = dx < 0 ? -dx : dx;
    abs_y = dy < 0 ? -dy : dy;

    if (action == AMOTION_EVENT_ACTION_MOVE) {
        if (pointer_count >= 2) {
            int distance;
            int focus_x;
            int focus_y;
            int new_zoom;
            int new_width;
            int new_height;
            int centered_left;
            int centered_top;
            if (!g_gallery_runtime.pinching) {
                distance = gallery_zoom_distance(x, y, x1, y1);
                if (distance > GALLERY_ZOOM_SLOP) {
                    focus_x = (x + x1) / 2;
                    focus_y = (y + y1) / 2;
                    gallery_zoom_clamp_pan();
                    g_gallery_runtime.pinching = 1;
                    g_gallery_runtime.pinch_start_distance = distance;
                    g_gallery_runtime.pinch_start_zoom = g_gallery_runtime.zoom_milli;
                    g_gallery_runtime.pinch_content_x_milli =
                            ((focus_x - g_gallery_runtime.zoom_left) * 1000)
                            / g_gallery_runtime.zoom_milli;
                    g_gallery_runtime.pinch_content_y_milli =
                            ((focus_y - g_gallery_runtime.zoom_top) * 1000)
                            / g_gallery_runtime.zoom_milli;
                }
            }
            if (g_gallery_runtime.pinching
                    && g_gallery_runtime.pinch_start_distance > 0) {
                distance = gallery_zoom_distance(x, y, x1, y1);
                focus_x = (x + x1) / 2;
                focus_y = (y + y1) / 2;
                new_zoom = (g_gallery_runtime.pinch_start_zoom * distance)
                        / g_gallery_runtime.pinch_start_distance;
                new_zoom = iclamp(new_zoom, GALLERY_ZOOM_MIN, GALLERY_ZOOM_MAX);
                new_width = gallery_zoom_scaled_dimension(
                        g_gallery_runtime.page_width, new_zoom);
                new_height = gallery_zoom_scaled_dimension(
                        g_gallery_runtime.page_height, new_zoom);
                centered_left = (g_gallery_runtime.page_width - new_width) / 2;
                centered_top = (g_gallery_runtime.page_height - new_height) / 2;
                g_gallery_runtime.zoom_milli = new_zoom;
                g_gallery_runtime.zoom_pan_x = focus_x
                        - (g_gallery_runtime.pinch_content_x_milli * new_zoom) / 1000
                        - centered_left;
                g_gallery_runtime.zoom_pan_y = focus_y
                        - (g_gallery_runtime.pinch_content_y_milli * new_zoom) / 1000
                        - centered_top;
                gallery_zoom_apply_layout(env, 0);
            }
            return JNI_TRUE;
        }

        if (g_gallery_runtime.zoom_milli > GALLERY_ZOOM_MIN) {
            if (!g_gallery_runtime.zoom_panning
                    && (abs_x > GALLERY_ZOOM_SLOP || abs_y > GALLERY_ZOOM_SLOP))
                g_gallery_runtime.zoom_panning = 1;
            if (g_gallery_runtime.zoom_panning) {
                g_gallery_runtime.zoom_pan_x = g_gallery_runtime.pan_start_x + dx;
                g_gallery_runtime.zoom_pan_y = g_gallery_runtime.pan_start_y + dy;
                gallery_zoom_apply_layout(env, 0);
            }
            return JNI_TRUE;
        }

        if (has_gallery && !g_gallery_runtime.horizontal_drag
                && abs_x > GALLERY_ZOOM_SLOP && abs_x > abs_y)
            g_gallery_runtime.horizontal_drag = 1;
        if (has_gallery && g_gallery_runtime.horizontal_drag) {
            if ((dx > 0 && g_gallery_runtime.current <= 0)
                    || (dx < 0 && g_gallery_runtime.current + 1 >= g_gallery_runtime.count))
                dx /= 3;
            dx = iclamp(dx, -g_gallery_runtime.page_width, g_gallery_runtime.page_width);
            g_gallery_runtime.drag_x = dx;
            gallery_scroll_to(env, g_gallery_runtime.page_width - dx);
        }
        return JNI_TRUE;
    }

    if (action == AMOTION_EVENT_ACTION_CANCEL) {
        g_gallery_runtime.touching = 0;
        g_gallery_runtime.pinching = 0;
        g_gallery_runtime.zoom_panning = 0;
        gallery_zoom_apply_layout(env, 1);
        if (has_gallery && g_gallery_runtime.zoom_milli <= GALLERY_ZOOM_MIN)
            gallery_start_settle(env, g_gallery_runtime.page_width, 0);
        else
            gallery_scroll_to(env, g_gallery_runtime.page_width);
        return JNI_TRUE;
    }

    if (action == AMOTION_EVENT_ACTION_UP) {
        uint32_t elapsed = now - g_gallery_runtime.touch_down_ms;
        if (g_gallery_runtime.pinching) {
            g_gallery_runtime.pinching = 0;
            g_gallery_runtime.touching = 0;
            gallery_zoom_apply_layout(env, 1);
            return JNI_TRUE;
        }
        if (g_gallery_runtime.zoom_milli > GALLERY_ZOOM_MIN) {
            int was_panning = g_gallery_runtime.zoom_panning;
            g_gallery_runtime.touching = 0;
            g_gallery_runtime.zoom_panning = 0;
            gallery_zoom_apply_layout(env, 1);
            if (!was_panning && abs_x <= GALLERY_ZOOM_SLOP
                    && abs_y <= GALLERY_ZOOM_SLOP)
                gallery_perform_click(env, view);
            return JNI_TRUE;
        }
        if (!g_gallery_runtime.horizontal_drag) {
            g_gallery_runtime.touching = 0;
            gallery_scroll_to(env, g_gallery_runtime.page_width);
            if (abs_x <= GALLERY_ZOOM_SLOP && abs_y <= GALLERY_ZOOM_SLOP)
                gallery_perform_click(env, view);
            return JNI_TRUE;
        }
        if (!has_gallery) {
            g_gallery_runtime.touching = 0;
            gallery_scroll_to(env, g_gallery_runtime.page_width);
            return JNI_TRUE;
        }
        threshold = iclamp(g_gallery_runtime.page_width / 5,
                           GALLERY_SWIPE_THRESHOLD_MIN,
                           GALLERY_SWIPE_THRESHOLD_MAX);
        if (dx < -threshold || (elapsed < 300 && dx < -(threshold / 2)))
            direction = 1;
        else if (dx > threshold || (elapsed < 300 && dx > threshold / 2))
            direction = -1;
        if (direction < 0 && g_gallery_runtime.current <= 0) direction = 0;
        if (direction > 0 && g_gallery_runtime.current + 1 >= g_gallery_runtime.count)
            direction = 0;
        gallery_start_settle(env,
                direction < 0 ? 0 : direction > 0
                    ? g_gallery_runtime.page_width * 2
                    : g_gallery_runtime.page_width,
                direction);
        return JNI_TRUE;
    }
    return JNI_TRUE;
}
