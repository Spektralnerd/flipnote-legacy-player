package android.view;
public class View {
  public interface OnTouchListener {boolean onTouch(View v,MotionEvent e);}
  public int width=320,height=480,invalidations=0; public Runnable pending;
  public int getWidth(){return width;} public int getHeight(){return height;}
  public boolean postDelayed(Runnable r,long delay){pending=r;return true;}
  public boolean removeCallbacks(Runnable r){pending=null;return true;}
  public void setOnTouchListener(OnTouchListener l){}
  public void setBackgroundColor(int c){}
  public void invalidate(){invalidations++;}
}
