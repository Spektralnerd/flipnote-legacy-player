#define FLIPBROWSER_HOST_TEST 1
#include "../native/flipbrowser.c"

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

static int check_frames(const char* path, const uint32_t* expected,
                        int expected_count, int expected_interval) {
    uint32_t thumbnail[THUMB_PIXELS];
    PpmAnimation* animation = create_animation(path, thumbnail, 1000);
    int frame;
    if (!animation) {
        printf("create failed: %s\n", path);
        return 0;
    }
    if (animation->frame_count != expected_count
            || animation->frame_interval_ms != expected_interval) {
        printf("metadata mismatch: %s frames=%d interval=%d\n", path,
               animation->frame_count, animation->frame_interval_ms);
        destroy_animation(animation);
        return 0;
    }
    if (hash_thumb(thumbnail) != expected[0]) {
        printf("frame 0 mismatch: %s got=%08x expected=%08x\n",
               path, hash_thumb(thumbnail), expected[0]);
        destroy_animation(animation);
        return 0;
    }
    for (frame = 1; frame < expected_count; frame++) {
        if (!animation_decode_frame(animation, frame)) {
            printf("decode failed: %s frame=%d\n", path, frame);
            destroy_animation(animation);
            return 0;
        }
        animation_render_thumbnail(animation, thumbnail);
        if (hash_thumb(thumbnail) != expected[frame]) {
            printf("frame mismatch: %s frame=%d got=%08x expected=%08x\n",
                   path, frame, hash_thumb(thumbnail), expected[frame]);
            destroy_animation(animation);
            return 0;
        }
    }
    animation_reset(animation);
    if (!animation_decode_frame(animation, 0)) {
        destroy_animation(animation);
        return 0;
    }
    animation_render_thumbnail(animation, thumbnail);
    if (hash_thumb(thumbnail) != expected[0]) {
        printf("loop reset mismatch: %s\n", path);
        destroy_animation(animation);
        return 0;
    }
    destroy_animation(animation);
    return 1;
}

static int check_scheduler(const char* path) {
    static const uint32_t expected[4] = {
        0x55349dcfu, 0xb369c7ebu, 0xe1470f86u, 0x630c5909u
    };
    uint32_t thumbnail[THUMB_PIXELS];
    PpmAnimation* animation = create_animation(path, thumbnail, 1000);
    uint32_t now = 1000;
    int step;
    if (!animation) return 0;
    for (step = 1; step <= 12; step++) {
        now += 167;
        if (advance_animation(animation, thumbnail, now) != 1
                || hash_thumb(thumbnail) != expected[step & 3]) {
            printf("scheduler mismatch: step=%d hash=%08x\n",
                   step, hash_thumb(thumbnail));
            destroy_animation(animation);
            return 0;
        }
    }
    destroy_animation(animation);
    return 1;
}

static int check_oled_black(const char* path) {
    uint32_t thumbnail[THUMB_PIXELS];
    PpmAnimation* animation = create_animation(path, thumbnail, 1000);
    int frame, pixel;
    int black_pixels = 0;
    if (!animation) return 0;
    for (frame = 0; frame < animation->frame_count; frame++) {
        if (frame > 0) {
            if (!animation_decode_frame(animation, frame)) {
                destroy_animation(animation);
                return 0;
            }
            animation_render_thumbnail(animation, thumbnail);
        }
        for (pixel = 0; pixel < THUMB_PIXELS; pixel++) {
            if (thumbnail[pixel] == 0x0e0e0eu) {
                printf("near-black pixel remained in frame %d\n", frame);
                destroy_animation(animation);
                return 0;
            }
            if (thumbnail[pixel] == 0x000000u) black_pixels++;
        }
    }
    destroy_animation(animation);
    if (black_pixels <= 0) {
        printf("no OLED-black pixels found\n");
        return 0;
    }
    printf("OLED black mapping: OK (%d pure-black preview pixels)\n", black_pixels);
    return 1;
}

int main(int argc, char** argv) {
    static const uint32_t synthetic_expected[4] = {
        0x55349dcfu, 0xb369c7ebu, 0xe1470f86u, 0x630c5909u
    };
    static const uint32_t translation_expected[2] = {
        0x3b1271fcu, 0xf7e1ae9au
    };
    if (argc != 3) {
        printf("usage: %s SYNTHETIC.ppm TRANSLATION.ppm\n", argv[0]);
        return 2;
    }
    if (!check_frames(argv[1], synthetic_expected, 4, 167)) return 3;
    if (!check_frames(argv[2], translation_expected, 2, 167)) return 4;
    if (!check_scheduler(argv[1])) return 5;
    if (!check_oled_black(argv[1])) return 6;
    printf("PPM frame decoder, translation and loop scheduler: OK\n");
    return 0;
}
