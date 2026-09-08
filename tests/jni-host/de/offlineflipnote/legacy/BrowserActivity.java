package de.offlineflipnote.legacy;
/** Exact Java equivalent of the added DEX class. Implementations are in eclair_browser.inc. */
public class BrowserActivity extends android.app.Activity implements Runnable,android.view.View.OnTouchListener {
 private long nativeHandle;
 static {System.loadLibrary("flipbrowser");}
 public BrowserActivity(){super();}
 protected native void onCreate(android.os.Bundle state);
 protected native void onResume();
 protected native void onPause();
 protected native void onDestroy();
 public native void onLowMemory();
 public native void run();
 public native boolean onTouch(android.view.View v,android.view.MotionEvent e);
 public native boolean onKeyDown(int key,android.view.KeyEvent e);
}
