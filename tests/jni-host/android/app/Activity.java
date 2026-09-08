package android.app;
import android.content.*; import android.view.*;
public class Activity extends Context {
 public Intent intent=new Intent(),started,result; public View content; public boolean finished=false; public int resultCode=0,superCalls=0;
 public Activity(){}
 protected void onCreate(android.os.Bundle b){superCalls++;}
 protected void onResume(){superCalls++;} protected void onPause(){superCalls++;} protected void onDestroy(){superCalls++;}
 public void onLowMemory(){superCalls++;}
 public Intent getIntent(){return intent;}
 public void startActivity(Intent i){started=i;}
 public void setContentView(View v){content=v;}
 public void finish(){finished=true;}
 public void setResult(int c,Intent i){resultCode=c;result=i;}
 public boolean onKeyDown(int key,KeyEvent e){return false;}
}
