package de.offlineflipnote.legacy;
/** Exact Java equivalent of the DEX class added in 1.4.15; the body lives in crash_log.inc. */
public final class CrashLog implements Thread.UncaughtExceptionHandler {
 public CrashLog(){}
 public native void uncaughtException(Thread t,Throwable e);
}
