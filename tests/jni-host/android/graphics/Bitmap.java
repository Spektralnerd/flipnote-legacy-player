package android.graphics;
public class Bitmap {
 public enum Config {ARGB_8888}
 public int width,height; public int[] pixels;
 public static Bitmap createBitmap(int w,int h,Config c){Bitmap b=new Bitmap();b.width=w;b.height=h;b.pixels=new int[w*h];return b;}
 public void setPixels(int[] p,int offset,int stride,int x,int y,int width,int height) {
  if(offset!=0||stride!=this.width||x!=0||y!=0||width!=this.width||height!=this.height)throw new AssertionError("pixel args");
  System.arraycopy(p,0,pixels,0,p.length);
 }
}
