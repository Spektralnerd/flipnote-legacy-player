package android.content;
import java.util.*;
public class Context {
  private static final Map<String,Boolean> values=new HashMap<String,Boolean>();
  public SharedPreferences getSharedPreferences(String s,int m){return new Prefs();}
  public static class Prefs implements SharedPreferences,SharedPreferences.Editor {
    public boolean getBoolean(String k,boolean d){return values.containsKey(k)?values.get(k):d;}
    public SharedPreferences.Editor edit(){return this;}
    public SharedPreferences.Editor putBoolean(String k,boolean b){values.put(k,b);return this;}
    public boolean commit(){return true;}
  }
}
