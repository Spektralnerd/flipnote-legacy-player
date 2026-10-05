package de.offlineflipnote.legacy;
/** Host double: only the static native the UI ticker calls since v1.4.16. */
public final class MainActivity {
    public static native long tickDelayMillis();
    private MainActivity() {}
}
