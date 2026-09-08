#define FLIPBROWSER_HOST_TEST 1
#include "../native/flipbrowser.c"

#define TEST_ITEMS 52

static uint32_t hash_thumb(const uint32_t* pixels) {
    uint32_t hash = 2166136261u;
    int i;
    for (i = 0; i < THUMB_PIXELS; i++) {
        uint32_t value = pixels[i];
        hash ^= value & 255u; hash *= 16777619u;
        hash ^= (value >> 8) & 255u; hash *= 16777619u;
        hash ^= (value >> 16) & 255u; hash *= 16777619u;
    }
    return hash;
}

int main(int argc, char** argv) {
    static const uint32_t expected[4] = {
        0x55349dcfu, 0xb369c7ebu, 0xe1470f86u, 0x630c5909u
    };
    PpmAnimation* animations[TEST_ITEMS];
    uint32_t* thumbnails[TEST_ITEMS];
    uint32_t now = 1000;
    int i, step;
    if (argc != 2) return 2;
    for (i = 0; i < TEST_ITEMS; i++) {
        thumbnails[i] = (uint32_t*)malloc(THUMB_PIXELS * sizeof(uint32_t));
        if (!thumbnails[i]) return 3;
        animations[i] = create_animation(argv[1], thumbnails[i], now);
        if (!animations[i] || hash_thumb(thumbnails[i]) != expected[0]) return 4;
    }
    for (step = 1; step <= 40; step++) {
        int expected_frame = step & 3;
        now += 167;
        for (i = 0; i < TEST_ITEMS; i++) {
            if (advance_animation(animations[i], thumbnails[i], now) != 1
                    || hash_thumb(thumbnails[i]) != expected[expected_frame]) {
                printf("stress mismatch: item=%d step=%d\n", i, step);
                return 5;
            }
        }
    }
    for (i = 0; i < TEST_ITEMS; i++) {
        destroy_animation(animations[i]);
        free(thumbnails[i]);
    }
    printf("52 simultaneous silent previews x 40 frame steps: OK\n");
    return 0;
}
