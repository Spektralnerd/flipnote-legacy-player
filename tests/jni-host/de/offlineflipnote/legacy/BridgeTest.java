package de.offlineflipnote.legacy;
import android.content.*;import android.view.*;import android.widget.*;import android.os.*;
public class BridgeTest {
 static void check(boolean b,String msg){if(!b)throw new AssertionError(msg);}
 static void tap(BrowserActivity a,int x,int y,long t){a.onTouch(a.content,new MotionEvent(0,x,y,t));a.onTouch(a.content,new MotionEvent(1,x,y,t+20));}
 static String pref(Context c,String k){return c.getSharedPreferences("flipnote_browser",0).getString(k,null);}
 public static void main(String[] args) throws Exception {
  Build.VERSION.SDK_INT=args.length>1?Integer.parseInt(args[1]):7;
  Context.resetPreferences();
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
  check(args[0].equals(pref(a,"last_directory")),"last folder saved after the first listing");
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

  // v1.4.13: a folder inside a ZIP archive on the API-7 path. The player
  // return target points into the archive; the selection is extracted to
  // Context.getCacheDir()/zip and returned as an ordinary file URI.
  String archive=args[0]+"/archiv.zip";
  BrowserActivity z=new BrowserActivity();
  z.intent.putExtra("flipnote.browser.return.directory",archive+"/0000");
  z.intent.putExtra("flipnote.browser.return.path",archive+"/0000/B.ppm");
  z.onCreate(null);z.onResume();
  for(int i=0;i<12;i++) z.run();
  check((archive+"/0000").equals(pref(z,"last_directory")),"ZIP folder remembered as last folder");
  z.onKeyDown(23,new KeyEvent());
  check(z.finished&&z.resultCode==-1&&z.result!=null,"ZIP entry selected");
  String cachePath=z.result.data.path;
  String cacheRoot=new java.io.File(Context.cacheDir,"zip").getAbsolutePath();
  check(cachePath.startsWith(cacheRoot+"/")&&cachePath.endsWith("/B.ppm"),"copy lives below <cache>/zip: "+cachePath);
  java.io.File copy=new java.io.File(cachePath);
  check(copy.isFile()&&copy.length()==1948,"extracted copy complete: "+copy.length());
  z.onPause();z.onDestroy();
  // Without a return target the browser starts in the remembered ZIP folder
  // and the parent card leads back to the archive root, then to the folder.
  BrowserActivity w=new BrowserActivity();
  w.onCreate(null);w.onResume();
  for(int i=0;i<6;i++) w.run();
  ImageView wv=(ImageView)w.content;
  check(wv.bitmap!=null,"restarted browser drawn");
  w.onKeyDown(21,new KeyEvent());w.onKeyDown(21,new KeyEvent());w.onKeyDown(19,new KeyEvent()); // select the first card ("..")
  w.onKeyDown(23,new KeyEvent());w.run();
  check(archive.equals(pref(w,"last_directory")),"parent of a ZIP folder is the archive root: "+pref(w,"last_directory"));
  w.onKeyDown(23,new KeyEvent());w.run();
  check(args[0].equals(pref(w,"last_directory")),"parent of the archive root is the real folder: "+pref(w,"last_directory"));
  w.onPause();w.onDestroy();
  System.out.println("PASS API-7/8 JNI host: ZIP folder listing, cache extraction to getCacheDir(), remembered ZIP folder and parent navigation");

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
