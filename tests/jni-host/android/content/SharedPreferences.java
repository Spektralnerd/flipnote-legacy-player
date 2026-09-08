package android.content;
public interface SharedPreferences {boolean getBoolean(String k,boolean def); Editor edit(); interface Editor {Editor putBoolean(String k,boolean v); boolean commit();}}
