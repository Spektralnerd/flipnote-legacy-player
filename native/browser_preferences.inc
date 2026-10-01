/* SharedPreferences is available since API 1. No external storage permission needed. */
static jobject browser_preferences(BrowserState* s, JNIEnv* env) {
    jclass cls = (*env)->FindClass(env, "android/content/Context");
    jmethodID get = cls ? (*env)->GetMethodID(env, cls, "getSharedPreferences",
        "(Ljava/lang/String;I)Landroid/content/SharedPreferences;") : 0;
    jstring name = (*env)->NewStringUTF(env, "flipnote_browser");
    jobject prefs = get && name ? (*env)->CallObjectMethod(env, s->activity->clazz, get, name, 0) : 0;
    if (name) (*env)->DeleteLocalRef(env, name);
    if (cls) (*env)->DeleteLocalRef(env, cls);
    jni_clear(env);
    return prefs;
}
static void browser_read_preferences(BrowserState* s, JNIEnv* env) {
    jobject prefs;
    jclass cls;
    jmethodID get, get_string;
    jstring key, dir_key, dir_value = 0;
    s->animate_previews = s->activity->sdkVersion >= 9;
    s->last_directory[0] = 0;
    if (!env) return;
    if ((*env)->PushLocalFrame(env, 16) < 0) { jni_clear(env); return; }
    prefs = browser_preferences(s, env);
    cls = (*env)->FindClass(env, "android/content/SharedPreferences");
    get = cls ? (*env)->GetMethodID(env, cls, "getBoolean", "(Ljava/lang/String;Z)Z") : 0;
    get_string = cls ? (*env)->GetMethodID(env, cls, "getString",
        "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;") : 0;
    key = (*env)->NewStringUTF(env, "animate_previews");
    dir_key = (*env)->NewStringUTF(env, "last_directory");
    if (prefs && get && key)
        s->animate_previews = (*env)->CallBooleanMethod(env, prefs, get, key, s->animate_previews);
    jni_clear(env);
    /* v1.4.13: the browser reopens the folder (or ZIP folder) it was last in. */
    if (prefs && get_string && dir_key)
        dir_value = (jstring)(*env)->CallObjectMethod(env, prefs, get_string, dir_key, (jstring)0);
    if ((*env)->ExceptionCheck(env)) { jni_clear(env); dir_value = 0; }
    if (dir_value) {
        const char* utf = (*env)->GetStringUTFChars(env, dir_value, 0);
        if (utf) {
            if (utf[0] == '/') copy_text(s->last_directory, utf, MAX_PATH);
            (*env)->ReleaseStringUTFChars(env, dir_value, utf);
        }
    }
    jni_clear(env);
    (*env)->PopLocalFrame(env, 0);
}
static void browser_save_preferences(BrowserState* s, JNIEnv* env) {
    jobject prefs, editor;
    jclass pc, ec;
    jmethodID edit, put, commit;
    jstring key;
    if (!env) return;
    if ((*env)->PushLocalFrame(env, 24) < 0) { jni_clear(env); return; }
    prefs = browser_preferences(s, env);
    pc = (*env)->FindClass(env, "android/content/SharedPreferences");
    ec = (*env)->FindClass(env, "android/content/SharedPreferences$Editor");
    edit = pc ? (*env)->GetMethodID(env, pc, "edit", "()Landroid/content/SharedPreferences$Editor;") : 0;
    put = ec ? (*env)->GetMethodID(env, ec, "putBoolean", "(Ljava/lang/String;Z)Landroid/content/SharedPreferences$Editor;") : 0;
    commit = ec ? (*env)->GetMethodID(env, ec, "commit", "()Z") : 0;
    editor = prefs && edit ? (*env)->CallObjectMethod(env, prefs, edit) : 0;
    key = (*env)->NewStringUTF(env, "animate_previews");
    if (editor && put && key && commit) {
        (*env)->CallObjectMethod(env, editor, put, key, s->animate_previews);
        (*env)->CallBooleanMethod(env, editor, commit);
    }
    jni_clear(env);
    (*env)->PopLocalFrame(env, 0);
}
/* Stores the current (possibly virtual ZIP) folder as the next start folder. */
static void browser_save_last_directory(BrowserState* s, JNIEnv* env) {
    jobject prefs, editor;
    jclass pc, ec;
    jmethodID edit, put, commit;
    jstring key, value;
    if (!env || s->directory[0] != '/') return;
    if ((*env)->PushLocalFrame(env, 24) < 0) { jni_clear(env); return; }
    prefs = browser_preferences(s, env);
    pc = (*env)->FindClass(env, "android/content/SharedPreferences");
    ec = (*env)->FindClass(env, "android/content/SharedPreferences$Editor");
    edit = pc ? (*env)->GetMethodID(env, pc, "edit", "()Landroid/content/SharedPreferences$Editor;") : 0;
    put = ec ? (*env)->GetMethodID(env, ec, "putString", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;") : 0;
    commit = ec ? (*env)->GetMethodID(env, ec, "commit", "()Z") : 0;
    editor = prefs && edit ? (*env)->CallObjectMethod(env, prefs, edit) : 0;
    key = (*env)->NewStringUTF(env, "last_directory");
    value = (*env)->NewStringUTF(env, s->directory);
    if (editor && put && key && value && commit) {
        (*env)->CallObjectMethod(env, editor, put, key, value);
        (*env)->CallBooleanMethod(env, editor, commit);
        copy_text(s->last_directory, s->directory, MAX_PATH);
    }
    jni_clear(env);
    (*env)->PopLocalFrame(env, 0);
}
/* Context.getCacheDir() exists since API 1 and needs no permission. Extracted
   ZIP entries live below <cache>/zip so Android may clear them any time. */
static void browser_read_cache_dir(BrowserState* s, JNIEnv* env) {
    jclass ctx_cls, file_cls;
    jmethodID get_cache, get_path;
    jobject dir;
    jstring path = 0;
    s->cache_root[0] = 0;
    if (!env) return;
    if ((*env)->PushLocalFrame(env, 16) < 0) { jni_clear(env); return; }
    ctx_cls = (*env)->FindClass(env, "android/content/Context");
    file_cls = (*env)->FindClass(env, "java/io/File");
    get_cache = ctx_cls ? (*env)->GetMethodID(env, ctx_cls, "getCacheDir", "()Ljava/io/File;") : 0;
    get_path = file_cls ? (*env)->GetMethodID(env, file_cls, "getAbsolutePath", "()Ljava/lang/String;") : 0;
    dir = get_cache ? (*env)->CallObjectMethod(env, s->activity->clazz, get_cache) : 0;
    if ((*env)->ExceptionCheck(env)) { jni_clear(env); dir = 0; }
    if (dir && get_path) path = (jstring)(*env)->CallObjectMethod(env, dir, get_path);
    if ((*env)->ExceptionCheck(env)) { jni_clear(env); path = 0; }
    if (path) {
        const char* utf = (*env)->GetStringUTFChars(env, path, 0);
        if (utf && utf[0] == '/' && strlen(utf) + 5 < MAX_PATH) {
            join_path(s->cache_root, utf, "zip");
        }
        if (utf) (*env)->ReleaseStringUTFChars(env, path, utf);
    }
    jni_clear(env);
    (*env)->PopLocalFrame(env, 0);
}
