package android.os;
/** Double for the external storage root; the test points it at its output directory. */
public class Environment {
  public static java.io.File getExternalStorageDirectory(){
    return new java.io.File(System.getProperty("flipnote.external",System.getProperty("java.io.tmpdir")));
  }
}
