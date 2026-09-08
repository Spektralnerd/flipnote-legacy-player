/* Link-time export stub only; never included in an APK. */
void* dlopen(const char* name, int flags) { (void)name; (void)flags; return 0; }
void* dlsym(void* handle, const char* name) { (void)handle; (void)name; return 0; }
