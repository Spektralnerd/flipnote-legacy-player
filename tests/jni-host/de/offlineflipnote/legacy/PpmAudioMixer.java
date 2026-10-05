package de.offlineflipnote.legacy;

/**
 * Host double of the player's mixer: the same private fields as the DEX
 * class, the native mix() of v1.4.16 and a line-by-line Java transcription
 * of the 1.4.15 Dalvik bytecode of mix() as the reference.
 */
public final class PpmAudioMixer {
    private final short[][] tracks;
    private final int[][] events;
    private final double bgmSourceRate;
    private final long totalSamples;

    public PpmAudioMixer(short[][] tracks, int[][] events, double bgmSourceRate, long totalSamples) {
        this.tracks = tracks;
        this.events = events;
        this.bgmSourceRate = bgmSourceRate;
        this.totalSamples = totalSamples;
    }

    public long getTotalSamples() { return totalSamples; }

    public native void mix(short[] out, int offset, int count, long packed);

    private static int lowerBound(int[] values, long key) {
        int lo = 0, hi = values.length;
        while (lo < hi) {
            int mid = (lo + hi) >>> 1;
            if ((long) values[mid] < key) lo = mid + 1; else hi = mid;
        }
        return lo;
    }

    /** Reference: PpmAudioMixer.mix() of versions 1.4.3-1.4.15 (Java). */
    public void mixReference(short[] out, int offset, int count, long packed) {
        java.util.Arrays.fill(out, offset, out.length, (short) 0);
        int speed = (int) (packed >>> 32);
        long start = packed & 0xffffffffL;
        for (int i = 0; i < count; i++) {
            long pos = start + (((long) i * (long) speed) >> 12);
            int s = (int) (pos >> 4);
            int acc = 0;
            short[] bgm = tracks[0];
            if (bgm.length > 0) {
                int bi = (int) Math.floor((double) s * bgmSourceRate / PpmFile.OUTPUT_SAMPLE_RATE);
                if (bi < bgm.length) acc = bgm[bi] >> 1;
            }
            for (int t = 0; t < 3; t++) {
                short[] se = tracks[t + 1];
                if (se.length <= 0) continue;
                int[] ev = events[t];
                if (ev.length <= 0) continue;
                int k = lowerBound(ev, (long) (s - (se.length << 2) + 1));
                for (; k < ev.length; k++) {
                    int e = ev[k];
                    if (e > s) break;
                    int si = (s - e) >> 2;
                    if (si < se.length) {
                        acc += se[si] >> 1;
                        if (acc < -32768) acc = -32768;
                        if (acc > 32767) acc = 32767;
                    }
                }
            }
            out[offset + i] = (short) acc;
        }
    }

    /** True when the Java double arithmetic for the BGM index of sample s sits
        within 1e-6 of an integer boundary, where fixed point may legitimately
        pick the neighbouring sample. */
    public boolean bgmIndexNearBoundary(int s) {
        double v = (double) s * bgmSourceRate / PpmFile.OUTPUT_SAMPLE_RATE;
        double fraction = v - Math.floor(v);
        return fraction < 1e-6 || fraction > 1 - 1e-6;
    }

    public int sampleFor(long packed, int i) {
        int speed = (int) (packed >>> 32);
        long start = packed & 0xffffffffL;
        return (int) ((start + (((long) i * (long) speed) >> 12)) >> 4);
    }
}
