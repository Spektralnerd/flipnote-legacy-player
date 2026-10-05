package android.util;
public class Log {
  public static String getStackTraceString(Throwable t){
    java.io.StringWriter w=new java.io.StringWriter(); t.printStackTrace(new java.io.PrintWriter(w)); return w.toString();
  }
}
