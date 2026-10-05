/* Host regression test for the native audio mixer (v1.4.16).

   mixer_render() is compared against a double-precision C transcription of
   the Java PpmAudioMixer.mix() of 1.4.3-1.4.15 over random tracks, events,
   speeds and BGM rates; the fixed-point step conversion is checked against
   exact floor() values. Runs under AddressSanitizer/UBSan in the sanitized
   host-test configuration. usage: mixer-test */
#define FLIPBROWSER_HOST_TEST 1
#include "../native/flipbrowser.c"
#include <math.h>

static int g_checks;
#define CHECK(cond) do { if (!(cond)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); } g_checks++; } while (0)

static uint64_t g_seed = 0x9e3779b97f4a7c15ULL;
static uint32_t rnd(uint32_t bound) {
    g_seed = g_seed * 6364136223846793005ULL + 1442695040888963407ULL;
    return (uint32_t)((g_seed >> 33) % bound);
}

static int lower_bound_ref(const jint* values, int count, int64_t key) {
    int lo = 0, hi = count;
    while (lo < hi) {
        int mid = (int)(((unsigned)lo + (unsigned)hi) >> 1);
        if ((int64_t)values[mid] < key) lo = mid + 1; else hi = mid;
    }
    return lo;
}

/* Dalvik double-to-int: NaN -> 0, saturating at the int limits. */
static int java_d2i(double v) {
    if (v != v) return 0;
    if (v >= 2147483647.0) return 0x7fffffff;
    if (v <= -2147483648.0) return (int)-2147483647 - 1;
    return (int)v;
}

/* The Java method, with doubles, as the oracle. */
static void mix_reference(jshort* out, int out_length, int offset, int count, int64_t packed,
                          const MixerInput* in, double rate, int output_rate) {
    int i, t;
    int32_t speed = (int32_t)(uint32_t)((uint64_t)packed >> 32);
    int64_t start = (int64_t)((uint64_t)packed & 0xffffffffULL);
    for (i = offset; i < out_length; i++) out[i] = 0;
    for (i = 0; i < count; i++) {
        int64_t pos = start + (((int64_t)i * (int64_t)speed) >> 12);
        int32_t s = (int32_t)(uint32_t)(uint64_t)(pos >> 4);
        int acc = 0;
        if (in->sample_count[0] > 0) {
            int bi = java_d2i(floor((double)s * rate / output_rate));
            if (bi >= 0 && bi < in->sample_count[0]) acc = in->samples[0][bi] >> 1;
        }
        for (t = 0; t < 3; t++) {
            const jshort* se = in->samples[t + 1];
            int len = in->sample_count[t + 1];
            const jint* ev = in->events[t];
            int n = in->event_count[t];
            int k;
            if (len <= 0 || n <= 0) continue;
            k = lower_bound_ref(ev, n, (int64_t)(int32_t)((uint32_t)s - ((uint32_t)len << 2) + 1u));
            for (; k < n; k++) {
                int e = ev[k];
                int si;
                if (e > s) break;
                si = (int)(((uint32_t)s - (uint32_t)e) >> 2);
                if (si < len) {
                    acc += se[si] >> 1;
                    if (acc < -32768) acc = -32768;
                    if (acc > 32767) acc = 32767;
                }
            }
        }
        out[offset + i] = (jshort)acc;
    }
}

static uint64_t double_bits(double d) { uint64_t u; memcpy(&u, &d, sizeof u); return u; }

static int near_boundary(int32_t s, double rate, int output_rate) {
    double v = (double)s * rate / output_rate;
    double f = v - floor(v);
    return f < 1e-6 || f > 1 - 1e-6;
}

static jshort* tone(int length, int amplitude, int period) {
    jshort* out = (jshort*)malloc(sizeof(jshort) * (size_t)(length > 0 ? length : 1));
    int i;
    for (i = 0; i < length; i++) {
        int tri = (i % period) * 4 * amplitude / period - 2 * amplitude;
        if (tri > amplitude) tri = 2 * amplitude - tri;
        if (tri < -amplitude) tri = -2 * amplitude - tri;
        out[i] = (jshort)tri;
    }
    return out;
}

static jint* events_for(double fps, int frames, int every, int phase, int* count) {
    jint* out = (jint*)malloc(sizeof(jint) * (size_t)(frames + 1));
    int f, n = 0;
    for (f = phase; f < frames; f += every) out[n++] = (jint)lround(f / fps * 32768.0);
    *count = n;
    return out;
}

int main(void) {
    static const double rates[] = { 8180.0, 16360.0, 8180.0 * 20 / 12, 8180.0 * 0.5 / 30, 8180.0 * 30 / 0.5, 11025.5 };
    static const uint32_t speeds[] = { 4096, 2048, 8192, 29900, 1, 65536, 0x7fffffffu };
    MixerInput in;
    int r, sp, round, i;
    int frames = 240;
    double fps = 12.0;
    int boundary = 0, blocks = 0;

    /* Step conversion against exact floor(). */
    for (r = 0; r < (int)(sizeof rates / sizeof rates[0]); r++) {
        uint64_t step = mixer_step_q40(double_bits(rates[r]), 32768);
        static const int samples[] = { 0, 1, 7, 4095, 4096, 32768, 65535, 1000000, 123456789, 0x7fffffff };
        CHECK(step > 0 && step < (1ULL << 47));
        for (i = 0; i < (int)(sizeof samples / sizeof samples[0]); i++) {
            double exact = floor((double)samples[i] * rates[r] / 32768.0);
            int got = mixer_bgm_index(samples[i], step);
            if (exact > 2147483646.0) { CHECK(got == -1 || got >= 0); continue; }
            CHECK(got == java_d2i(exact) || (got == java_d2i(exact) - 1 && near_boundary(samples[i], rates[r], 32768)));
        }
    }
    CHECK(mixer_step_q40(double_bits(0.0), 32768) == 0);
    CHECK(mixer_step_q40(double_bits(-8180.0), 32768) == 0);
    CHECK(mixer_step_q40(double_bits(1e12), 32768) == 0);
    CHECK(mixer_step_q40(0x7ff8000000000000ULL, 32768) == 0);   /* NaN */
    CHECK(mixer_step_q40(0x7ff0000000000000ULL, 32768) == 0);   /* +inf */
    CHECK(mixer_step_q40(double_bits(8180.0), 0) == 0);
    CHECK(mixer_bgm_index(-1, mixer_step_q40(double_bits(8180.0), 32768)) == -1);
    /* Division helper against the compiler on a few values. */
    for (i = 0; i < 2000; i++) {
        uint64_t n = ((uint64_t)rnd(0xffffffffu) << 32) | rnd(0xffffffffu);
        uint32_t d = rnd(0xffffffffu) + 1;
        CHECK(mixer_udiv64(n, d) == n / d);
    }
    CHECK(mixer_udiv64(12345, 0) == 0);
    printf("PASS fixed-point BGM step and index match floor() (boundary cases allowed), 64/32 division helper\n");

    memset(&in, 0, sizeof in);
    in.samples[0] = tone(8180 * 20, 12000, 37); in.sample_count[0] = 8180 * 20;
    in.samples[1] = tone(2000, 32767, 11); in.sample_count[1] = 2000;
    in.samples[2] = tone(1500, 30000, 5); in.sample_count[2] = 1500;
    in.samples[3] = tone(300, 9000, 23); in.sample_count[3] = 300;
    in.events[0] = events_for(fps, frames, 3, 0, &in.event_count[0]);
    in.events[1] = events_for(fps, frames, 5, 1, &in.event_count[1]);
    in.events[2] = events_for(fps, frames, 1, 0, &in.event_count[2]);

    for (r = 0; r < (int)(sizeof rates / sizeof rates[0]); r++) {
        in.step_q40 = mixer_step_q40(double_bits(rates[r]), 32768);
        for (sp = 0; sp < (int)(sizeof speeds / sizeof speeds[0]); sp++) {
            for (round = 0; round < 8; round++) {
                int length = 1024 + (int)rnd(7000);
                int offset = (int)rnd((uint32_t)length / 2);
                int count = (int)rnd((uint32_t)(length - offset + 1));
                uint32_t position = rnd((uint32_t)frames * 2800u) * 16u + rnd(16);
                int64_t packed = (int64_t)(((uint64_t)speeds[sp] << 32) | position);
                jshort* a = (jshort*)malloc(sizeof(jshort) * (size_t)length);
                jshort* b = (jshort*)malloc(sizeof(jshort) * (size_t)length);
                for (i = 0; i < length; i++) a[i] = b[i] = (jshort)rnd(65536);
                mix_reference(a, length, offset, count, packed, &in, rates[r], 32768);
                mixer_render(b, length, offset, count, speeds[sp], position, &in);
                for (i = 0; i < length; i++) {
                    if (a[i] == b[i]) continue;
                    if (i >= offset && i < offset + count) {
                        int64_t pos = (int64_t)position + (((int64_t)(i - offset) * (int64_t)(int32_t)speeds[sp]) >> 12);
                        int32_t s = (int32_t)(uint32_t)(uint64_t)(pos >> 4);
                        if (near_boundary(s, rates[r], 32768)) { boundary++; continue; }
                    }
                    printf("FAIL rate %.3f speed %u round %d index %d: ref %d native %d\n",
                           rates[r], speeds[sp], round, i, a[i], b[i]);
                    return 1;
                }
                blocks++;
                free(a); free(b);
            }
        }
    }
    printf("PASS native mixer: %d random blocks identical to the Java-semantics oracle (%d BGM boundary samples tolerated)\n", blocks, boundary);

    /* Degenerate shapes: empty tracks, empty events, clamped count, negative count. */
    {
        MixerInput empty;
        jshort buf[300];
        memset(&empty, 0, sizeof empty);
        for (i = 0; i < 300; i++) buf[i] = 77;
        mixer_render(buf, 300, 10, 200, 4096, 0, &empty);
        for (i = 0; i < 10; i++) CHECK(buf[i] == 77);
        for (i = 10; i < 300; i++) CHECK(buf[i] == 0);
        for (i = 0; i < 300; i++) buf[i] = 77;
        mixer_render(buf, 300, 100, 100000, 4096, 1234 * 16, &in);     /* count clamped to 200 */
        for (i = 0; i < 100; i++) CHECK(buf[i] == 77);
        mixer_render(buf, 300, 300, 5, 4096, 0, &in);                  /* offset == length: nothing */
        for (i = 0; i < 100; i++) CHECK(buf[i] == 77);
        mixer_render(buf, 300, 0, -5, 4096, 0, &in);                   /* negative count clears only */
        for (i = 0; i < 300; i++) CHECK(buf[i] == 0);
        /* Events far beyond the note and a position near the 32-bit limit must stay in bounds. */
        in.step_q40 = mixer_step_q40(double_bits(8180.0), 32768);
        mixer_render(buf, 300, 0, 300, 65536, 0xfffffff0u, &in);
        mixer_render(buf, 300, 0, 300, 0x7fffffffu, 0xffffffffu, &in);
    }
    printf("PASS native mixer: degenerate ranges, fill-to-end, extreme positions and speeds without out-of-bounds access\n");

    for (i = 0; i < 4; i++) free((void*)in.samples[i]);
    for (i = 0; i < 3; i++) free((void*)in.events[i]);
    printf("Mixer host test: %d checks OK\n", g_checks);
    return 0;
}
