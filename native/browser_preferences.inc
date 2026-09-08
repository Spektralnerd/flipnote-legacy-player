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
    jmethodID get;
    jstring key;
    s->animate_previews = s->activity->sdkVersion >= 9;
    if (!env) return;
    if ((*env)->PushLocalFrame(env, 16) < 0) { jni_clear(env); return; }
    prefs = browser_preferences(s, env);
    cls = (*env)->FindClass(env, "android/content/SharedPreferences");
    get = cls ? (*env)->GetMethodID(env, cls, "getBoolean", "(Ljava/lang/String;Z)Z") : 0;
    key = (*env)->NewStringUTF(env, "animate_previews");
    if (prefs && get && key)
        s->animate_previews = (*env)->CallBooleanMethod(env, prefs, get, key, s->animate_previews);
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
