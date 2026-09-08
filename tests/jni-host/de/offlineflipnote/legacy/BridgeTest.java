package de.offlineflipnote.legacy;
import android.content.*;import android.view.*;import android.widget.*;import android.os.*;
public class BridgeTest {
 static void check(boolean b,String msg){if(!b)throw new AssertionError(msg);}
 static void tap(BrowserActivity a,int x,int y,long t){a.onTouch(a.content,new MotionEvent(0,x,y,t));a.onTouch(a.content,new MotionEvent(1,x,y,t+20));}
 public static void main(String[] args) throws Exception {
  Build.VERSION.SDK_INT=args.length>1?Integer.parseInt(args[1]):7;
  BrowserActivity a=new BrowserActivity();
  a.intent.putExtra("flipnote.browser.return.directory",args[0]);
  a.intent.putExtra("flipnote.browser.return.path",args[0]+"/synthetic-test.ppm");
  a.onCreate(null);a.onResume();
  check(a.content instanceof ImageView,"Eclair ImageView");
  ImageView v=(ImageView)a.content;
  for(int i=0;i<8;i++) a.run();
  check(v.bitmap!=null&&v.bitmap.pixels.length==320*480,"bitmap drawn");
  check(v.invalidations>0,"invalidated");
  int calls=v.invalidations;
  for(int i=0;i<20;i++) a.run();
  check(calls==v.invalidations,"static mode must not redraw continuously");
  check(!a.getSharedPreferences("flipnote_browser",0).getBoolean("animate_previews",false),"API7 default static");
  tap(a,280,20,1000);a.run(); // Open settings.
  tap(a,150,120,1100);a.run(); // Toggle previews.
  check(a.getSharedPreferences("flipnote_browser",0).getBoolean("animate_previews",false),"saved preference");
  tap(a,150,212,1200); // Close settings.
  for(int i=0;i<8;i++) a.run();
  calls=v.invalidations;
  Thread.sleep(210);a.run();
  check(v.invalidations>calls,"animations resume after enabling");
  v.width=480;v.height=320;a.run();
  check(v.bitmap.pixels.length==480*320,"resized");
  a.onLowMemory();a.run();
  a.onPause();check(v.pending==null,"pause cancels timer");
  a.onResume();a.run();
  a.onKeyDown(23,new KeyEvent());
  check(a.finished&&a.resultCode==-1,"file selected and browser finished");
  check(a.result!=null&&a.result.data.path.endsWith("synthetic-test.ppm"),"selected URI");
  a.onPause();a.onDestroy();check(a.superCalls>=7,"lifecycle super calls");
  System.out.println("PASS API-7/8 JNI host: bitmap, static/animated toggle, persistence, resize, low-memory, selection and lifecycle");
  Build.VERSION.SDK_INT=36;
  BrowserActivity modern=new BrowserActivity();
  modern.intent.putExtra("flipnote.browser.return.directory",args[0]);modern.onCreate(null);
  check(modern.finished&&modern.started!=null,"modern route forwarded and finished");
  check(modern.started.cls.equals("android.app.NativeActivity"),"native activity destination");
  check((modern.started.flags&0x02000000)!=0,"forward result flag");
  check(args[0].equals(modern.started.getStringExtra("flipnote.browser.return.directory")),"return directory preserved");
  modern.onDestroy();
  System.out.println("PASS modern JNI routing: intent and result forwarding (host, not an Android emulator)");
 }
}
