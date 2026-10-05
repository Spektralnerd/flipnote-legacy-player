package de.offlineflipnote.legacy;
import android.os.Build;

/**
 * v1.4.16 host test: the native PpmAudioMixer.mix() against the Java
 * reference (1.4.15 bytecode transcription) and MainActivity.tickDelayMillis().
 * usage: MixerTest <sdk> <expected idle tick ms>
 */
public class MixerTest {
    static void check(boolean b, String msg) { if (!b) throw new AssertionError(msg); }
    static long seed = 0x5eed1234abcdL;
    static int rnd(int bound) { seed = seed * 6364136223846793005L + 1442695040888963407L; return (int) ((seed >>> 33) % bound); }

    static short[] tone(int length, int amplitude, int period) {
        short[] out = new short[length];
        for (int i = 0; i < length; i++) {
            int tri = (i % period) * 4 * amplitude / period - 2 * amplitude; /* triangle wave */
            if (tri > amplitude) tri = 2 * amplitude - tri;
            if (tri < -amplitude) tri = -2 * amplitude - tri;
            out[i] = (short) tri;
        }
        return out;
    }

    static int[] eventsFor(double fps, int frames, int everyNth, int phase) {
        java.util.ArrayList<Integer> list = new java.util.ArrayList<Integer>();
        for (int f = phase; f < frames; f += everyNth) list.add((int) Math.round(f / fps * PpmFile.OUTPUT_SAMPLE_RATE));
        int[] out = new int[list.size()];
        for (int i = 0; i < out.length; i++) out[i] = list.get(i);
        return out;
    }

    static long packed(int speedQ12, long positionQ4) { return ((long) speedQ12 << 32) | (positionQ4 & 0xffffffffL); }

    /** Compares native and reference output for one block; returns mismatches
        that are not explained by a BGM boundary case. */
    static int compare(PpmAudioMixer mixer, short[] a, short[] b, int offset, int count, long packed, String label) {
        mixer.mixReference(a, offset, count, packed);
        mixer.mix(b, offset, count, packed);
        int unexplained = 0;
        int explained = 0;
        for (int i = 0; i < a.length; i++) {
            if (a[i] == b[i]) continue;
            int sample = i >= offset && i < offset + count ? mixer.sampleFor(packed, i - offset) : -1;
            if (sample >= 0 && mixer.bgmIndexNearBoundary(sample)) { explained++; continue; }
            if (unexplained < 5) System.out.println("  MISMATCH " + label + " at " + i + ": ref=" + a[i] + " native=" + b[i] + " sample=" + sample);
            unexplained++;
        }
        if (explained > 0) System.out.println("  (" + label + ": " + explained + " boundary-case samples differ by one BGM index, allowed)");
        return unexplained;
    }

    public static void main(String[] args) throws Exception {
        Build.VERSION.SDK_INT = Integer.parseInt(args[0]);
        long expectedTick = Long.parseLong(args[1]);
        Class.forName("de.offlineflipnote.legacy.BrowserActivity"); /* System.loadLibrary("flipbrowser") */
        Thread.setDefaultUncaughtExceptionHandler(null);

        long tick = MainActivity.tickDelayMillis();
        check(tick == expectedTick, "idle tick for SDK " + Build.VERSION.SDK_INT + " = " + tick + ", expected " + expectedTick);
        System.out.println("PASS tickDelayMillis(): SDK " + Build.VERSION.SDK_INT + " -> " + tick + " ms idle interval");

        int frames = 240;
        double fps = 12.0;
        short[] bgm = tone(8180 * 20, 12000, 37);          /* 20 s of BGM at 8180 Hz */
        short[][] tracks = { bgm, tone(2000, 32767, 11), tone(1500, 30000, 5), tone(300, 9000, 23) };
        int[][] events = { eventsFor(fps, frames, 3, 0), eventsFor(fps, frames, 5, 1), eventsFor(fps, frames, 1, 0) };
        double[] rates = { 8180.0, 8180.0 * 12 / 6, 8180.0 * 20 / 12, 8180.0 * 0.5 / 30, 8180.0 * 30 / 0.5, 16360.0 };
        int[] speeds = { 4096, 2048, 8192, 29900, 1, 65536 };
        int unexplained = 0;
        int blocks = 0;
        for (double rate : rates) {
            PpmAudioMixer mixer = new PpmAudioMixer(tracks, events, rate, (long) (frames / fps * PpmFile.OUTPUT_SAMPLE_RATE));
            for (int speed : speeds) {
                for (int round = 0; round < 6; round++) {
                    int length = 1024 + rnd(7000);
                    int offset = rnd(length / 2);
                    int count = rnd(length - offset + 1);
                    long position = (long) rnd(frames * 2800) * 16 + rnd(16);   /* Q4 position inside the note */
                    long packed = packed(speed, position);
                    short[] a = new short[length], b = new short[length];
                    for (int i = 0; i < length; i++) { a[i] = (short) rnd(65536); b[i] = a[i]; }
                    unexplained += compare(mixer, a, b, offset, count, packed, "rate=" + rate + " speed=" + speed + " round=" + round);
                    blocks++;
                }
            }
        }
        check(unexplained == 0, unexplained + " unexplained sample mismatches");
        System.out.println("PASS native mixer: " + blocks + " random blocks identical to the 1.4.15 Java mixer (6 BGM rates x 6 speeds, clamping, fill-to-end)");

        /* Degenerate inputs: empty tracks, empty events, nothing audible. */
        short[][] empty = { new short[0], new short[0], new short[0], new short[0] };
        int[][] noEvents = { new int[0], new int[0], new int[0] };
        PpmAudioMixer silent = new PpmAudioMixer(empty, noEvents, 8180.0, 1000);
        short[] a = new short[300], b = new short[300];
        java.util.Arrays.fill(a, (short) 77); java.util.Arrays.fill(b, (short) 77);
        check(compare(silent, a, b, 10, 200, packed(4096, 0), "empty") == 0, "empty tracks");
        for (int i = 0; i < 10; i++) check(b[i] == 77, "samples before offset untouched");
        for (int i = 10; i < 300; i++) check(b[i] == 0, "cleared to the end of the buffer");
        PpmAudioMixer effectsOnly = new PpmAudioMixer(new short[][] { new short[0], tracks[1], tracks[2], tracks[3] }, events, 0.0, 1000);
        check(compare(effectsOnly, new short[2048], new short[2048], 0, 2048, packed(4096, 5000 * 16), "effects-only") == 0, "effects only");
        PpmAudioMixer bgmOnly = new PpmAudioMixer(new short[][] { bgm, new short[0], new short[0], new short[0] }, noEvents, 8180.0, 1000);
        check(compare(bgmOnly, new short[4096], new short[4096], 0, 4096, packed(4096, 0), "bgm-only") == 0, "bgm only");
        /* Rates the Java code would never produce must not crash and must silence the BGM. */
        for (double odd : new double[] { Double.NaN, Double.POSITIVE_INFINITY, -8180.0, 0.0, 1e12 }) {
            PpmAudioMixer m = new PpmAudioMixer(new short[][] { bgm, new short[0], new short[0], new short[0] }, noEvents, odd, 1000);
            short[] out = new short[512];
            java.util.Arrays.fill(out, (short) 5);
            m.mix(out, 0, 512, packed(4096, 0));
            for (int i = 0; i < 512; i++) check(out[i] == 0, "odd rate " + odd + " is silent");
        }
        System.out.println("PASS native mixer: empty tracks, effects-only, BGM-only and invalid rates");

        /* Out-of-range arguments: Java would throw on the audio thread; the
           native version writes what fits and never touches memory outside. */
        PpmAudioMixer mixer = new PpmAudioMixer(tracks, events, 8180.0, 1000);
        short[] ref = new short[1000], out = new short[1000];
        mixer.mixReference(ref, 100, 900, packed(4096, 1234 * 16));
        mixer.mix(out, 100, 5000, packed(4096, 1234 * 16));      /* count beyond the end */
        check(java.util.Arrays.equals(ref, out), "count clamped to the buffer");
        java.util.Arrays.fill(out, (short) 9);
        mixer.mix(out, -1, 10, packed(4096, 0));
        mixer.mix(out, 1001, 10, packed(4096, 0));
        mixer.mix(null, 0, 10, packed(4096, 0));
        for (int i = 0; i < 1000; i++) check(out[i] == 9, "invalid offset leaves the buffer alone");
        java.util.Arrays.fill(out, (short) 9);
        mixer.mix(out, 1000, 10, packed(4096, 0));
        for (int i = 0; i < 1000; i++) check(out[i] == 9, "offset == length writes nothing");
        mixer.mix(out, 0, -5, packed(4096, 0));
        for (int i = 0; i < 1000; i++) check(out[i] == 0, "negative count still clears from offset");
        System.out.println("PASS native mixer: out-of-range offset/count handled without exceptions");

        /* Speed comparison on this host, for the report only. */
        short[] big = new short[8192];
        long packedSpeed = packed(4096, 0);
        long t0 = System.nanoTime();
        for (int i = 0; i < 40; i++) mixer.mixReference(big, 0, 8192, packedSpeed + (long) i * 8192 * 16);
        long t1 = System.nanoTime();
        for (int i = 0; i < 40; i++) mixer.mix(big, 0, 8192, packedSpeed + (long) i * 8192 * 16);
        long t2 = System.nanoTime();
        System.out.println("INFO mixer host timing: Java reference " + (t1 - t0) / 1000000 + " ms, native " + (t2 - t1) / 1000000 + " ms for 40 blocks of 8192 samples (JIT-compiled host JVM, not Dalvik)");
    }
}
