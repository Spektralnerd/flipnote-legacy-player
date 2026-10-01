package android.content;
import java.util.*;
public class Context {
  private static final Map<String,Object> values=new HashMap<String,Object>();
  public static java.io.File cacheDir=new java.io.File(System.getProperty("java.io.tmpdir"),"flipnote-jni-host-cache");
  public static void resetPreferences(){values.clear();}
  public SharedPreferences getSharedPreferences(String s,int m){return new Prefs();}
  /** Context.getCacheDir() exists since API 1; the double mirrors a created, writable directory. */
  public java.io.File getCacheDir(){cacheDir.mkdirs();return cacheDir;}
  public static class Prefs implements SharedPreferences,SharedPreferences.Editor {
    public boolean getBoolean(String k,boolean d){return values.containsKey(k)?(Boolean)values.get(k):d;}
    public String getString(String k,String d){return values.containsKey(k)?(String)values.get(k):d;}
    public SharedPreferences.Editor edit(){return this;}
    public SharedPreferences.Editor putBoolean(String k,boolean b){values.put(k,b);return this;}
    public SharedPreferences.Editor putString(String k,String v){values.put(k,v);return this;}
    public boolean commit(){return true;}
  }
}
