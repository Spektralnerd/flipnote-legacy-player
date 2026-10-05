package de.offlineflipnote.legacy;
/** Exact Java equivalent of the DEX class added in 1.4.15. The native body is in crash_log.inc;
 *  JNI_OnLoad installs an instance as Thread.setDefaultUncaughtExceptionHandler(). */
public final class CrashLog implements Thread.UncaughtExceptionHandler {
 public CrashLog(){}
 public native void uncaughtException(Thread t,Throwable e);
}
