/*
 * Native audio mixer (v1.4.16).
 *
 * PpmAudioMixer.mix() fills one block of 32768-Hz output samples from the
 * decoded BGM track and the three sound-effect tracks of a Flipnote. The
 * Java version of 1.4.3-1.4.15 did this per output sample with a
 * Math.floor() call and three binary searches, on a thread with
 * THREAD_PRIORITY_URGENT_AUDIO. On a single-core ARMv6 phone (Galaxy Ace,
 * Android 2.3) that alone took a quarter of the CPU away from the user
 * interface. This file implements the identical arithmetic in C: integer
 * only (no soft-float or libgcc helpers in the ARMv5 build), the BGM
 * resampling step as 24.40 fixed point derived from the raw IEEE-754 bits of
 * bgmSourceRate, and the same clamping order as the Java code.
 *
 * Call contract, as in the Java version:
 *   out[offset .. out.length) is cleared, then count samples are written at
 *   out[offset ..]. packed = speed(Q12, upper 32 bit) | position(Q4, lower
 *   32 bit); output sample i reads note position (position + (i*speed >> 12))
 *   >> 4. BGM index = floor(pos * bgmSourceRate / OUTPUT_SAMPLE_RATE);
 *   sound effect k of track t plays from event sample events[t][k] at a
 *   quarter of the output rate; every added sample is halved and the sum is
 *   clamped to 16 bit after each addition.
 */
#define MIXER_TRACKS 4
#define MIXER_EFFECTS 3
#define MIXER_DEFAULT_OUTPUT_RATE 32768

typedef struct MixerIds {
    jfieldID tracks;        /* PpmAudioMixer.tracks : short[][] */
    jfieldID events;        /* PpmAudioMixer.events : int[][] */
    jfieldID rate;          /* PpmAudioMixer.bgmSourceRate : double */
    int output_rate;        /* PpmFile.OUTPUT_SAMPLE_RATE, 0 = not read yet */
    int tried;
} MixerIds;

static MixerIds g_mixer_ids;

/* 64-bit / 32-bit unsigned division with constant shifts only, so the
   ARMv5 soft-float build needs no __aeabi_uldivmod. Used once per block. */
static uint64_t mixer_udiv64(uint64_t n, uint32_t d) {
    uint64_t q = 0;
    uint64_t r = 0;
    int i;
    if (!d) return 0;
    for (i = 0; i < 64; i++) {
        r = (r << 1) | (n >> 63);
        n <<= 1;
        q <<= 1;
        if (r >= d) {
            r -= d;
            q |= 1;
        }
    }
    return q;
}

/* bgmSourceRate / output_rate as unsigned 24.40 fixed point, computed from
   the IEEE-754 bit pattern of the Java double. Non-positive, NaN, infinite
   or absurd rates yield 0, which silences the BGM like an empty track. */
static uint64_t mixer_step_q40(uint64_t bits, uint32_t output_rate) {
    int exponent = (int)((bits >> 52) & 0x7ff);
    uint64_t mantissa = bits & 0x000fffffffffffffULL;
    int shift;
    if ((bits >> 63) || exponent == 0x7ff || exponent == 0 || !output_rate) return 0;
    mantissa |= 0x0010000000000000ULL;          /* implicit leading one */
    /* value = mantissa * 2^(exponent - 1075); step = value * 2^40 / rate */
    shift = exponent - 1075 + 40;
    if (shift > 10) return 0;                   /* rate above 4 MHz: nonsense */
    while (shift < 0) { mantissa >>= 1; shift++; }
    while (shift > 0) { mantissa <<= 1; shift--; }
    return mixer_udiv64(mantissa, output_rate);
}

/* floor(sample * step / 2^40) for a non-negative sample index, exact in
   64-bit pieces (sample < 2^31, step < 2^47). Returns -1 when the index does
   not fit the array range check that follows. */
static int mixer_bgm_index(int sample, uint64_t step) {
    uint64_t s = (uint64_t)(uint32_t)sample;
    uint64_t lo = s * (uint32_t)(step & 0xffffffffu);
    uint64_t hi = s * (uint32_t)(step >> 32);
    uint64_t add = (hi & 0xff) << 32;
    uint64_t mid = lo + add;
    uint64_t carry = mid < lo ? 1 : 0;
    uint64_t index = (hi >> 8) + (mid >> 40) + (carry << 24);
    if (sample < 0 || index > 0x7fffffffu) return -1;
    return (int)index;
}

/* First index whose value is >= key (Java PpmAudioMixer.lowerBound). */
static int mixer_lower_bound(const jint* values, int count, int64_t key) {
    int lo = 0;
    int hi = count;
    while (lo < hi) {
        int mid = (int)(((unsigned)lo + (unsigned)hi) >> 1);
        if ((int64_t)values[mid] < key) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

typedef struct MixerInput {
    const jshort* samples[MIXER_TRACKS];
    int sample_count[MIXER_TRACKS];
    const jint* events[MIXER_EFFECTS];
    int event_count[MIXER_EFFECTS];
    uint64_t step_q40;
} MixerInput;

static void mixer_render(jshort* out, int out_length, int offset, int count,
                         uint32_t speed_q12, uint32_t start_q4, const MixerInput* in) {
    int i;
    int t;
    for (i = offset; i < out_length; i++) out[i] = 0;
    if (count > out_length - offset) count = out_length - offset;
    for (i = 0; i < count; i++) {
        int64_t pos = (int64_t)start_q4
                + (((int64_t)i * (int64_t)(int32_t)speed_q12) >> 12);
        int sample = (int)(int32_t)(uint32_t)((uint64_t)pos >> 4);
        int acc = 0;
        if (in->sample_count[0] > 0) {
            int index = mixer_bgm_index(sample, in->step_q40);
            if (index >= 0 && index < in->sample_count[0])
                acc = in->samples[0][index] >> 1;
        }
        for (t = 0; t < MIXER_EFFECTS; t++) {
            const jshort* effect = in->samples[t + 1];
            int effect_length = in->sample_count[t + 1];
            const jint* events = in->events[t];
            int event_count = in->event_count[t];
            int32_t key;
            int k;
            if (effect_length <= 0 || event_count <= 0) continue;
            /* Java: lowerBound(events, (long)(s - (len << 2) + 1)), int wrap. */
            key = (int32_t)((uint32_t)sample - ((uint32_t)effect_length << 2) + 1u);
            for (k = mixer_lower_bound(events, event_count, (int64_t)key);
                 k < event_count; k++) {
                int event = events[k];
                int index;
                if (event > sample) break;
                index = (int)((uint32_t)((uint32_t)sample - (uint32_t)event) >> 2);
                if (index < effect_length) {
                    acc += effect[index] >> 1;
                    if (acc < -32768) acc = -32768;
                    if (acc > 32767) acc = 32767;
                }
            }
        }
        out[offset + i] = (jshort)acc;
    }
}

/* Field IDs of PpmAudioMixer and PpmFile.OUTPUT_SAMPLE_RATE, looked up once.
   Only the audio thread calls this; should two playback threads ever overlap
   at the very first call, the IDs are published before the flag, so the loser
   merely repeats the lookup. */
static int mixer_resolve_ids(JNIEnv* env, jobject self) {
    MixerIds ids;
    jclass cls;
    jclass file_cls;
    jfieldID rate_field;
    if (g_mixer_ids.tried) return g_mixer_ids.tracks && g_mixer_ids.events && g_mixer_ids.rate;
    memset(&ids, 0, sizeof(ids));
    cls = (*env)->GetObjectClass(env, self);
    if (cls) {
        ids.tracks = (*env)->GetFieldID(env, cls, "tracks", "[[S");
        jni_clear(env);
        ids.events = (*env)->GetFieldID(env, cls, "events", "[[I");
        jni_clear(env);
        ids.rate = (*env)->GetFieldID(env, cls, "bgmSourceRate", "D");
        jni_clear(env);
        (*env)->DeleteLocalRef(env, cls);
    }
    ids.output_rate = MIXER_DEFAULT_OUTPUT_RATE;
    file_cls = (*env)->FindClass(env, "de/offlineflipnote/legacy/PpmFile");
    jni_clear(env);
    rate_field = file_cls ? (*env)->GetStaticFieldID(env, file_cls, "OUTPUT_SAMPLE_RATE", "I") : 0;
    jni_clear(env);
    if (rate_field) {
        jint value = (*env)->GetStaticIntField(env, file_cls, rate_field);
        if (!(*env)->ExceptionCheck(env) && value > 0) ids.output_rate = (int)value;
        jni_clear(env);
    }
    if (file_cls) (*env)->DeleteLocalRef(env, file_cls);
    g_mixer_ids.tracks = ids.tracks;
    g_mixer_ids.events = ids.events;
    g_mixer_ids.rate = ids.rate;
    g_mixer_ids.output_rate = ids.output_rate;
    g_mixer_ids.tried = 1;
    return ids.tracks && ids.events && ids.rate;
}

__attribute__((visibility("default")))
void Java_de_offlineflipnote_legacy_PpmAudioMixer_mix(
        JNIEnv* env, jobject self, jshortArray out, jint offset, jint count, jlong packed) {
    MixerInput in;
    jobjectArray tracks = 0;
    jobjectArray events = 0;
    jshortArray track_refs[MIXER_TRACKS];
    jintArray event_refs[MIXER_EFFECTS];
    jshort* out_data = 0;
    jint out_length;
    jint track_total = 0;
    jint event_total = 0;
    union { jdouble d; uint64_t bits; } rate;
    int t;

    if (!out) return;
    out_length = (*env)->GetArrayLength(env, out);
    if (offset < 0 || offset > out_length) return;
    if ((*env)->PushLocalFrame(env, 16) < 0) { jni_clear(env); return; }
    memset(&in, 0, sizeof(in));
    for (t = 0; t < MIXER_TRACKS; t++) track_refs[t] = 0;
    for (t = 0; t < MIXER_EFFECTS; t++) event_refs[t] = 0;
    rate.bits = 0;

    if (mixer_resolve_ids(env, self)) {
        tracks = (jobjectArray)(*env)->GetObjectField(env, self, g_mixer_ids.tracks);
        events = (jobjectArray)(*env)->GetObjectField(env, self, g_mixer_ids.events);
        rate.d = (*env)->GetDoubleField(env, self, g_mixer_ids.rate);
        if ((*env)->ExceptionCheck(env)) { jni_clear(env); tracks = 0; events = 0; rate.bits = 0; }
    }
    in.step_q40 = mixer_step_q40(rate.bits, (uint32_t)g_mixer_ids.output_rate);
    if (tracks) track_total = (*env)->GetArrayLength(env, tracks);
    if (events) event_total = (*env)->GetArrayLength(env, events);
    for (t = 0; t < MIXER_TRACKS && t < track_total; t++) {
        track_refs[t] = (jshortArray)(*env)->GetObjectArrayElement(env, tracks, t);
        if ((*env)->ExceptionCheck(env)) { jni_clear(env); track_refs[t] = 0; }
        if (!track_refs[t]) continue;
        in.sample_count[t] = (*env)->GetArrayLength(env, track_refs[t]);
        if (in.sample_count[t] > 0)
            in.samples[t] = (*env)->GetShortArrayElements(env, track_refs[t], 0);
        if (!in.samples[t]) in.sample_count[t] = 0;
    }
    for (t = 0; t < MIXER_EFFECTS && t < event_total; t++) {
        event_refs[t] = (jintArray)(*env)->GetObjectArrayElement(env, events, t);
        if ((*env)->ExceptionCheck(env)) { jni_clear(env); event_refs[t] = 0; }
        if (!event_refs[t]) continue;
        in.event_count[t] = (*env)->GetArrayLength(env, event_refs[t]);
        if (in.event_count[t] > 0)
            in.events[t] = (*env)->GetIntArrayElements(env, event_refs[t], 0);
        if (!in.events[t]) in.event_count[t] = 0;
    }
    jni_clear(env);

    out_data = (*env)->GetShortArrayElements(env, out, 0);
    if (out_data) {
        mixer_render(out_data, out_length, offset, count,
                     (uint32_t)((uint64_t)packed >> 32), (uint32_t)((uint64_t)packed & 0xffffffffu), &in);
        (*env)->ReleaseShortArrayElements(env, out, out_data, 0);
    }
    jni_clear(env);
    for (t = 0; t < MIXER_TRACKS; t++)
        if (in.samples[t])
            (*env)->ReleaseShortArrayElements(env, track_refs[t], (jshort*)in.samples[t], JNI_ABORT);
    for (t = 0; t < MIXER_EFFECTS; t++)
        if (in.events[t])
            (*env)->ReleaseIntArrayElements(env, event_refs[t], (jint*)in.events[t], JNI_ABORT);
    jni_clear(env);
    (*env)->PopLocalFrame(env, 0);
}
