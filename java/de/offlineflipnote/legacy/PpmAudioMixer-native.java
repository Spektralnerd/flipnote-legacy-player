package de.offlineflipnote.legacy;
/** Signature of the mixer method that tools/patch-dex-v1416.py turns native.
 *  The class itself (fields tracks:short[][], events:int[][], bgmSourceRate:double,
 *  totalSamples:long, note:PpmFile; constructor, buildEventTables, lowerBound,
 *  getTotalSamples) stays the compiled 1.4.11 bytecode. The body of mix() is
 *  Java_de_offlineflipnote_legacy_PpmAudioMixer_mix in native/audio_mixer.inc:
 *  out[offset..] receives count samples for note position (packed & 0xffffffff)/16
 *  advancing by (packed >>> 32)/4096 per sample; the rest of out is cleared.
 *  Not a compilable unit on its own (file name carries a suffix on purpose). */
public final class PpmAudioMixer {
 public native void mix(short[] out,int offset,int count,long packed);
}
