package android.view; public class MotionEvent {
  private int a;private float x,y;private long t;
  public MotionEvent(int aa,float xx,float yy,long tt){a=aa;x=xx;y=yy;t=tt;}
  public int getAction(){return a;} public float getX(){return x;} public float getY(){return y;} public long getEventTime(){return t;}
}
