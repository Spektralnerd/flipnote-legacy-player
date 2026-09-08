package android.content; import java.util.*; import android.net.Uri;
public class Intent {
  public int flags; public String cls; public String pkg; public Uri data;
  public Map<String,String> extras=new HashMap<String,String>();
  public Intent(){} public Intent(String s){}
  public Intent setClassName(String p,String c){pkg=p;cls=c;return this;}
  public Intent addFlags(int f){flags|=f;return this;}
  public String getStringExtra(String k){return extras.get(k);}
  public Intent putExtra(String k,String v){extras.put(k,v);return this;}
  public Intent setData(Uri u){data=u;return this;}
  public Intent setDataAndType(Uri u,String t){data=u;return this;}
}
