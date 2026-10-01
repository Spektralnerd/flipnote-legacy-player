/* Host regression test for ZIP browsing, the extraction cache, the 1000-entry
   sorted limit and the remembered start folder (v1.4.13).
   usage: zip-test FIXTURE_DIR SYNTHETIC.ppm TRANSLATION.ppm */
#define FLIPBROWSER_HOST_TEST 1
#include "../native/flipbrowser.c"
#include <assert.h>

static char g_fixtures[MAX_PATH];
static char g_synthetic[MAX_PATH];
static char g_translation[MAX_PATH];
static int g_checks;

#define CHECK(cond) do { if (!(cond)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); exit(1); } g_checks++; } while (0)

static void fixture(const char* name, char* out) { join_path(out, g_fixtures, name); }

static int read_file(const char* path, unsigned char** data, int* size) {
    FILE* f = fopen(path, "rb");
    long n;
    if (!f) return 0;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    *data = (unsigned char*)malloc((size_t)n + 1);
    *size = (int)fread(*data, 1, (size_t)n, f);
    fclose(f);
    return *size == n;
}

static uint32_t hash_thumb(const uint32_t* pixels) {
    uint32_t hash = 2166136261u;
    int i;
    for (i = 0; i < THUMB_PIXELS; i++) {
        hash ^= pixels[i] & 0xffffffu; hash *= 16777619u;
    }
    return hash;
}

static BrowserState* new_state(ANativeActivity* activity) {
    BrowserState* s = (BrowserState*)calloc(1, sizeof(BrowserState));
    memset(activity, 0, sizeof(*activity));
    activity->sdkVersion = 9;
    s->activity = activity;
    s->animate_previews = 0;
    return s;
}

static void free_state(BrowserState* s) {
    free_thumbnails(s);
    if (s->zip) zip_archive_free(s->zip);
    free(s);
}

static int find_item(BrowserState* s, const char* name) {
    int i;
    for (i = 0; i < s->item_count; i++)
        if (strcmp(s->items[i].name, name) == 0) return i;
    return -1;
}

static int find_entry(ZipArchive* zip, const char* name) {
    int i;
    for (i = 0; i < zip->entry_count; i++)
        if (strcmp(zip_entry_name(zip, i), name) == 0) return i;
    return -1;
}

static void test_name_normalization(void) {
    char name[64];
    copy_text(name, "./0300/PUNKT00.ppm", 64); CHECK(zip_normalize_name(name) && strcmp(name, "0300/PUNKT00.ppm") == 0);
    copy_text(name, "/abs/X.PPM", 64); CHECK(zip_normalize_name(name) && strcmp(name, "abs/X.PPM") == 0);
    copy_text(name, "a\\b\\C.ppm", 64); CHECK(zip_normalize_name(name) && strcmp(name, "a/b/C.ppm") == 0);
    copy_text(name, "a//b.ppm", 64); CHECK(zip_normalize_name(name) && strcmp(name, "a/b.ppm") == 0);
    copy_text(name, "../BOESE.ppm", 64); CHECK(!zip_normalize_name(name));
    copy_text(name, "a/../b.ppm", 64); CHECK(!zip_normalize_name(name));
    copy_text(name, "a/./b.ppm", 64); CHECK(!zip_normalize_name(name));
    copy_text(name, "a/.hidden/b.ppm", 64); CHECK(zip_normalize_name(name) && strcmp(name, "a/.hidden/b.ppm") == 0);
    copy_text(name, "a/..b/c.ppm", 64); CHECK(zip_normalize_name(name) && strcmp(name, "a/..b/c.ppm") == 0);
    copy_text(name, "0300/", 64); CHECK(!zip_normalize_name(name));
    copy_text(name, "notizen.txt", 64); CHECK(!zip_normalize_name(name));
    copy_text(name, "", 64); CHECK(!zip_normalize_name(name));
    puts("PASS entry names: ./ and / prefixes stripped, backslashes, .. rejected, PPM only");
}

static void test_split_path(void) {
    char zip[MAX_PATH], inner[MAX_PATH], path[MAX_PATH], sub[MAX_PATH];
    fixture("archiv-deflate.zip", path);
    CHECK(zip_split_path(path, zip, inner) && strcmp(zip, path) == 0 && inner[0] == 0);
    join_path(sub, path, "0100/unter/");
    CHECK(zip_split_path(sub, zip, inner) && strcmp(zip, path) == 0 && strcmp(inner, "0100/unter") == 0);
    fixture("ordner/echt.zip", path);
    CHECK(!zip_split_path(path, zip, inner));          /* a real directory */
    CHECK(browser_target_readable(path));
    fixture("gibtesnicht.zip/0000", path);
    CHECK(!zip_split_path(path, zip, inner));
    CHECK(!browser_target_readable(path));
    CHECK(!browser_target_readable("SPEICHER"));
    fixture("archiv-deflate.zip/0000", path);
    CHECK(browser_target_readable(path));
    puts("PASS virtual paths: archive root, nested folder, directory named .zip, missing file");
}

static void test_index(void) {
    char path[MAX_PATH];
    ZipArchive* zip;
    int i;
    fixture("archiv-deflate.zip", path);
    zip = zip_archive_open(path);
    CHECK(zip && zip->entry_count == 15 && zip->unsupported == 0 && !zip->truncated);
    CHECK(find_entry(zip, "0300/PUNKT00.ppm") >= 0);
    CHECK(find_entry(zip, "WURZEL.PPM") >= 0);
    CHECK(find_entry(zip, "../BOESE.ppm") < 0 && find_entry(zip, "BOESE.ppm") < 0);
    CHECK(find_entry(zip, "0100/notizen.txt") < 0);
    for (i = 0; i < zip->entry_count; i++) {
        const ZipLocator* loc = &zip->entries[i].locator;
        CHECK(loc->method == ZIP_METHOD_DEFLATE);
        CHECK(loc->uncompressed_size == 55282u || loc->uncompressed_size == 1948u);
    }
    zip_archive_free(zip);

    fixture("archiv-zip64.zip", path);
    zip = zip_archive_open(path);
    CHECK(zip && zip->entry_count == 3);
    i = find_entry(zip, "0000/Z64A.ppm");
    CHECK(i >= 0 && zip->entries[i].locator.uncompressed_size == 55282u
          && zip->entries[i].locator.method == ZIP_METHOD_DEFLATE);
    i = find_entry(zip, "0000/Z64B.ppm");
    CHECK(i >= 0 && zip->entries[i].locator.uncompressed_size == 1948u
          && zip->entries[i].locator.method == ZIP_METHOD_STORED
          && zip->entries[i].locator.compressed_size == 1948u);
    zip_archive_free(zip);

    fixture("archiv-methoden.zip", path);
    zip = zip_archive_open(path);
    CHECK(zip && zip->entry_count == 3 && zip->unsupported == 2);
    i = find_entry(zip, "0000/BZIP2.ppm"); CHECK(i >= 0 && !zip_locator_supported(&zip->entries[i].locator));
    i = find_entry(zip, "0000/GEHEIM.ppm"); CHECK(i >= 0 && !zip_locator_supported(&zip->entries[i].locator));
    i = find_entry(zip, "0000/OFFEN.ppm"); CHECK(i >= 0 && zip_locator_supported(&zip->entries[i].locator));
    zip_archive_free(zip);

    fixture("archiv-vorspann.zip", path);
    zip = zip_archive_open(path);
    CHECK(zip && zip->entry_count == 2);
    zip_archive_free(zip);

    fixture("archiv-leer.zip", path);
    zip = zip_archive_open(path);
    CHECK(zip && zip->entry_count == 0);
    zip_archive_free(zip);

    fixture("kaputt.zip", path);
    CHECK(zip_archive_open(path) == 0);
    fixture("daten/eins.ppm", path);
    CHECK(zip_archive_open(path) == 0);
    puts("PASS central directory: deflate, stored, ZIP64 fields and EOCD, unsupported methods, prepended data, comment, empty, garbage");
}

static void check_roundtrip(const char* archive) {
    static const char* names[7] = {
        "eins.ppm", "hundert.ppm", "block.ppm", "text300k.ppm",
        "zufall300k.ppm", "rle.ppm", "weit.ppm" };
    char path[MAX_PATH], original[MAX_PATH];
    ZipArchive* zip;
    int n;
    fixture(archive, path);
    zip = zip_archive_open(path);
    CHECK(zip && zip->entry_count == 7);
    for (n = 0; n < 7; n++) {
        unsigned char* expected = 0;
        unsigned char* got;
        int expected_size = 0, index = find_entry(zip, names[n]), head;
        const ZipLocator* loc;
        CHECK(index >= 0);
        loc = &zip->entries[index].locator;
        join_path(original, g_fixtures, "daten");
        copy_text(original + strlen(original), "/", MAX_PATH - (int)strlen(original));
        copy_text(original + strlen(original), names[n], MAX_PATH - (int)strlen(original));
        CHECK(read_file(original, &expected, &expected_size));
        CHECK((uint32_t)expected_size == loc->uncompressed_size);
        got = (unsigned char*)malloc((size_t)expected_size + 1);
        CHECK(zip_read_locator(path, loc, got, expected_size, 1));
        CHECK(memcmp(got, expected, (size_t)expected_size) == 0);
        /* Early stop: any prefix must match without reading the rest. */
        for (head = 1; head <= expected_size; head = head * 7 + 3) {
            memset(got, 0xaa, (size_t)expected_size);
            CHECK(zip_read_locator(path, loc, got, head, 0));
            CHECK(memcmp(got, expected, (size_t)head) == 0);
        }
        CHECK(!zip_read_locator(path, loc, got, expected_size + 1, 0));
        free(got);
        free(expected);
    }
    zip_archive_free(zip);
}

static void test_inflate_roundtrip(void) {
    char path[MAX_PATH];
    ZipArchive* zip;
    ZipLocator broken;
    unsigned char* buffer;
    int index;
    check_roundtrip("daten-l0.zip");
    check_roundtrip("daten-l1.zip");
    check_roundtrip("daten-l6.zip");
    check_roundtrip("daten-l9.zip");
    check_roundtrip("daten-stored.zip");
    /* Corrupted metadata must be rejected, never trusted. */
    fixture("daten-l6.zip", path);
    zip = zip_archive_open(path);
    index = find_entry(zip, "text300k.ppm");
    CHECK(index >= 0);
    buffer = (unsigned char*)malloc(400 * 1024);
    broken = zip->entries[index].locator;
    broken.crc ^= 1u;
    CHECK(!zip_read_locator(path, &broken, buffer, (int)broken.uncompressed_size, 1));
    broken = zip->entries[index].locator;
    broken.uncompressed_size += 1000;
    CHECK(!zip_read_locator(path, &broken, buffer, (int)broken.uncompressed_size, 1));
    broken = zip->entries[index].locator;
    broken.local_offset += 3;
    CHECK(!zip_read_locator(path, &broken, buffer, 100, 0));
    broken = zip->entries[index].locator;
    broken.compressed_size = 10;
    CHECK(!zip_read_locator(path, &broken, buffer, (int)broken.uncompressed_size, 1));
    broken = zip->entries[index].locator;
    broken.flags |= ZIP_FLAG_ENCRYPTED;
    CHECK(!zip_read_locator(path, &broken, buffer, 100, 0));
    free(buffer);
    zip_archive_free(zip);
    puts("PASS inflate: stored/fixed/dynamic blocks at levels 0/1/6/9 and stored entries, every prefix length, CRC, bad sizes/offsets/flags rejected");
}

static void test_listing(void) {
    ANativeActivity activity;
    BrowserState* s = new_state(&activity);
    char path[MAX_PATH], sub[MAX_PATH];
    int i, parent;

    /* File system folder: archives sort after folders, before files. */
    fixture("ordner", path);
    CHECK(scan_directory(s, path));
    CHECK(s->item_count == 5);
    CHECK(s->items[0].is_parent);
    CHECK(strcmp(s->items[1].name, "echt.zip") == 0 && s->items[1].is_dir && !s->items[1].is_archive);
    CHECK(strcmp(s->items[2].name, "unterordner") == 0 && s->items[2].is_dir);
    CHECK(strcmp(s->items[3].name, "archiv.zip") == 0 && s->items[3].is_archive && s->items[3].is_dir);
    CHECK(strcmp(s->items[4].name, "lose.ppm") == 0 && !s->items[4].is_dir && s->items[4].size == 55282);
    CHECK(find_item(s, "notiz.txt") < 0);
    CHECK(s->directory_dirty);

    /* Open the archive like a folder. */
    i = find_item(s, "archiv.zip");
    open_item(s, 0, i);
    CHECK(s->zip && s->item_count == 2);
    CHECK(s->items[0].is_parent && strcmp(s->items[0].path, path) == 0);
    CHECK(strcmp(s->items[1].name, "0000") == 0 && s->items[1].is_dir && !s->items[1].is_archive);
    open_item(s, 0, 1);
    CHECK(s->item_count == 3);
    CHECK(strcmp(s->items[1].name, "A.ppm") == 0 && s->items[1].in_zip && s->items[1].size == 55282);
    CHECK(strcmp(s->items[2].name, "B.ppm") == 0 && s->items[2].in_zip && s->items[2].size == 1948);
    join_path(sub, path, "archiv.zip/0000");
    CHECK(strcmp(s->directory, sub) == 0);
    CHECK(strcmp(browser_zip_inner(s), "0000") == 0);
    /* Parent of a folder inside the archive is the archive root, then the folder. */
    open_item(s, 0, 0);
    join_path(sub, path, "archiv.zip");
    CHECK(strcmp(s->directory, sub) == 0 && s->item_count == 2);
    open_item(s, 0, 0);
    CHECK(strcmp(s->directory, path) == 0 && s->item_count == 5 && !s->items[0].in_zip);
    free_state(s);

    /* Hundreds folders, nested folder, root file, upper-case extension. */
    s = new_state(&activity);
    fixture("archiv-deflate.zip", path);
    CHECK(scan_directory(s, path));
    CHECK(s->item_count == 6);
    CHECK(strcmp(s->items[1].name, "0000") == 0 && strcmp(s->items[2].name, "0100") == 0
          && strcmp(s->items[3].name, "0200") == 0 && strcmp(s->items[4].name, "0300") == 0);
    CHECK(strcmp(s->items[5].name, "WURZEL.PPM") == 0 && s->items[5].in_zip);
    CHECK(s->message[0] == 0);
    join_path(sub, path, "0100");
    CHECK(scan_directory(s, sub));
    CHECK(s->item_count == 5);
    CHECK(strcmp(s->items[1].name, "unter") == 0 && s->items[1].is_dir);
    CHECK(strcmp(s->items[2].name, "F010000.ppm") == 0 && strcmp(s->items[4].name, "F010002.ppm") == 0);
    join_path(sub, path, "0100/unter");
    CHECK(scan_directory(s, sub));
    CHECK(s->item_count == 2 && strcmp(s->items[1].name, "TIEF00.ppm") == 0);
    parent = 0;
    CHECK(s->items[parent].is_parent);
    join_path(sub, path, "0100");
    CHECK(strcmp(s->items[parent].path, sub) == 0);
    join_path(sub, path, "0300");
    CHECK(scan_directory(s, sub) && s->item_count == 2 && strcmp(s->items[1].name, "PUNKT00.ppm") == 0);
    join_path(sub, path, "9999");
    CHECK(scan_directory(s, sub) && s->item_count == 1 && strcmp(s->message, "ZIP: KEINE ORDNER ODER PPM-DATEIEN") == 0);
    free_state(s);

    /* Unsupported entries are shown but flagged. */
    s = new_state(&activity);
    fixture("archiv-methoden.zip/0000", path);
    CHECK(scan_directory(s, path) && s->item_count == 4);
    CHECK(strcmp(s->message, "ZIP: EINTRAEGE MIT VERSCHLUESSELUNG/METHODE OHNE SUPPORT") == 0);
    i = find_item(s, "GEHEIM.ppm");
    CHECK(i >= 0 && !zip_locator_supported(browser_item_locator(s, &s->items[i])));
    load_one_thumbnail(s, 0, s->item_count - 1);
    load_one_thumbnail(s, 0, s->item_count - 1);
    load_one_thumbnail(s, 0, s->item_count - 1);
    CHECK(s->items[i].thumb_state == -1);
    i = find_item(s, "OFFEN.ppm");
    CHECK(i >= 0 && s->items[i].thumb_state == 1 && s->items[i].thumb);
    open_item(s, 0, find_item(s, "GEHEIM.ppm"));
    CHECK(!s->finishing && strcmp(s->message, "ZIP-EINTRAG NICHT UNTERSTUETZT (VERSCHLUESSELT/METHODE)") == 0);
    free_state(s);

    /* Unreadable archives produce a message and keep the parent card. */
    s = new_state(&activity);
    fixture("kaputt.zip", path);
    CHECK(!scan_directory(s, path));
    CHECK(strcmp(s->message, "ZIP NICHT LESBAR - KEIN ZIP ODER DEFEKT") == 0);
    CHECK(s->item_count == 1 && s->items[0].is_parent);
    CHECK(strcmp(s->items[0].path, g_fixtures) == 0);
    free_state(s);
    puts("PASS listings: archive cards, archive root, hundreds folders, nested folders, parent navigation, empty and unsupported cases");
}

static void test_previews(void) {
    ANativeActivity activity;
    BrowserState* s = new_state(&activity);
    char path[MAX_PATH];
    uint32_t direct[THUMB_PIXELS];
    uint32_t direct_translation[THUMB_PIXELS];
    int i, loaded = 0;
    PpmAnimation* reference;
    PpmAnimation* from_zip;
    int frame;

    CHECK(decode_thumbnail(g_synthetic, direct));
    CHECK(decode_thumbnail(g_translation, direct_translation));
    fixture("archiv-stored.zip/0000", path);
    CHECK(scan_directory(s, path) && s->item_count == 6);
    for (i = 0; i < 40; i++) load_one_thumbnail(s, 0, s->item_count - 1);
    for (i = 1; i < s->item_count; i++) {
        uint32_t expected = s->items[i].size == 55282 ? hash_thumb(direct) : hash_thumb(direct_translation);
        CHECK(s->items[i].thumb_state == 1 && s->items[i].thumb);
        CHECK(hash_thumb(s->items[i].thumb) == expected);
        CHECK(s->items[i].animation == 0);
        loaded++;
    }
    CHECK(loaded == 5);
    free_state(s);

    /* Deflated archive with animated previews: same frames as the file. */
    s = new_state(&activity);
    s->animate_previews = 1;
    fixture("archiv-deflate.zip/0000", path);
    CHECK(scan_directory(s, path) && s->item_count == 6);
    for (i = 0; i < 40; i++) load_one_thumbnail(s, 0, s->item_count - 1);
    i = find_item(s, "F000001.ppm");
    CHECK(i >= 0 && s->items[i].animation && s->items[i].animation->frame_count == 4);
    reference = create_animation(g_synthetic, direct, 1000);
    from_zip = s->items[i].animation;
    CHECK(reference && from_zip->data_size == reference->data_size);
    CHECK(memcmp(from_zip->data, reference->data, (size_t)reference->data_size) == 0);
    for (frame = 1; frame < 4; frame++) {
        CHECK(animation_decode_frame(reference, frame) && animation_decode_frame(from_zip, frame));
        animation_render_thumbnail(reference, direct);
        animation_render_thumbnail(from_zip, s->items[i].thumb);
        CHECK(hash_thumb(direct) == hash_thumb(s->items[i].thumb));
    }
    destroy_animation(reference);
    browser_set_animation(s, 0);
    CHECK(s->thumb_count == 0);
    free_state(s);
    puts("PASS previews: static thumbnails from stored entries, full animations from deflated entries match the plain files");
}

static void test_cache_and_gallery(void) {
    ANativeActivity activity;
    BrowserState* s = new_state(&activity);
    char path[MAX_PATH], cache[MAX_PATH], expect_dir[MAX_PATH], expect_path[MAX_PATH];
    char first_cache[MAX_PATH];
    unsigned char* a = 0;
    unsigned char* b = 0;
    int size_a = 0, size_b = 0, i, index;

    /* Like Context.getCacheDir(): the cache directory itself already exists,
       the browser creates only zip/<hash>/ below it. */
    fixture("cache-test", path);
    mkdir(path, 0700);
    join_path(s->cache_root, g_fixtures, "cache-test/zip");
    zip_cache_wipe_if_fresh(s->cache_root);
    memset(g_zip_cache_stamp, 0, sizeof(g_zip_cache_stamp));
    fixture("archiv-deflate.zip/0000", path);
    CHECK(scan_directory(s, path) && s->item_count == 6);
    index = find_item(s, "F000001.ppm");
    CHECK(browser_item_cache_path(s, &s->items[index], cache));
    CHECK(text_starts_with(cache, s->cache_root));
    CHECK(strcmp(path_basename(cache), "F000001.ppm") == 0);
    /* Selecting the entry extracts it and hands the copy to the player. */
    open_item(s, 0, index);
    CHECK(s->finishing == 0); /* no JNI on the host: return_selected_file is skipped */
    CHECK(read_file(cache, &a, &size_a) && read_file(g_synthetic, &b, &size_b));
    CHECK(size_a == size_b && memcmp(a, b, (size_t)size_a) == 0);
    free(a); free(b);
    CHECK(g_gallery_selection.count == 5 && g_gallery_selection.current == 1);
    CHECK(g_gallery_selection.from_zip == 1);
    CHECK(strcmp(g_gallery_selection.paths[1], cache) == 0);
    CHECK(strcmp(g_gallery_selection.virtual_directory, path) == 0);
    fixture("archiv-deflate.zip", expect_dir);
    CHECK(strcmp(g_gallery_selection.zip_path, expect_dir) == 0);
    CHECK(g_gallery_selection.locators[1].uncompressed_size == 55282u);

    /* The pager extracts neighbours lazily and reports the archive folder
       as the browser return target, never the cache directory. */
    CHECK(gallery_copy_selection_if_changed());
    CHECK(!file_exists_with_size(g_gallery_runtime.paths[2], 1948u));
    CHECK(gallery_materialize(2));
    CHECK(file_exists_with_size(g_gallery_runtime.paths[2], 1948u));
    g_gallery_runtime.list_matches_note = 1;
    g_gallery_runtime.current = 2;
    CHECK(gallery_prepare_browser_return_request(expect_dir, expect_path));
    CHECK(strcmp(expect_dir, path) == 0);
    join_path(cache, path, "F000002.ppm");
    CHECK(strcmp(expect_path, cache) == 0);

    /* Returning from the player reopens the archive folder and selects the entry. */
    free_state(s);
    s = new_state(&activity);
    join_path(s->cache_root, g_fixtures, "cache-test/zip");
    gallery_take_browser_return_request(s);
    CHECK(s->restore_pending);
    choose_start_directory(s);
    CHECK(strcmp(s->directory, path) == 0);
    CHECK(s->reveal_selected && strcmp(s->items[s->selected].name, "F000002.ppm") == 0);

    /* LRU table: the least recently used copies are deleted first, and a
       copy that is used again is protected. The played file (F000001) was
       used most recently, the neighbour F000002 before it. */
    fixture("archiv-gross.zip/viele", path);
    CHECK(scan_directory(s, path));
    copy_text(first_cache, g_gallery_runtime.paths[1], MAX_PATH);
    CHECK(file_exists_with_size(first_cache, 55282u));
    CHECK(zip_cache_ensure(first_cache, g_gallery_runtime.zip_path, &g_gallery_runtime.locators[1]));
    /* Two copies are in the table (F000002 older than F000001); 14 more
       fill it exactly, the 15th evicts F000002, the 16th evicts F000001. */
    for (i = 1; i <= ZIP_CACHE_RING - 2; i++) {
        CHECK(browser_item_cache_path(s, &s->items[i], cache));
        CHECK(zip_cache_ensure(cache, s->zip->path, browser_item_locator(s, &s->items[i])));
    }
    CHECK(file_exists_with_size(first_cache, 55282u));
    CHECK(file_exists_with_size(g_gallery_runtime.paths[2], 1948u));
    CHECK(browser_item_cache_path(s, &s->items[ZIP_CACHE_RING - 1], cache));
    CHECK(zip_cache_ensure(cache, s->zip->path, browser_item_locator(s, &s->items[ZIP_CACHE_RING - 1])));
    CHECK(file_exists_with_size(first_cache, 55282u));          /* touched later: kept */
    CHECK(!file_exists_with_size(g_gallery_runtime.paths[2], 1948u)); /* least recent: evicted */
    CHECK(browser_item_cache_path(s, &s->items[ZIP_CACHE_RING], cache));
    CHECK(zip_cache_ensure(cache, s->zip->path, browser_item_locator(s, &s->items[ZIP_CACHE_RING])));
    CHECK(!file_exists_with_size(first_cache, 55282u));         /* now least recent: evicted */
    /* Re-ensuring an existing copy neither re-extracts nor evicts anything. */
    for (i = 1; i <= ZIP_CACHE_RING; i++) {
        CHECK(browser_item_cache_path(s, &s->items[i], cache));
        CHECK(file_exists_with_size(cache, 1948u));
    }
    CHECK(zip_cache_ensure(cache, s->zip->path, browser_item_locator(s, &s->items[1])));
    for (i = 1; i <= ZIP_CACHE_RING; i++) {
        CHECK(browser_item_cache_path(s, &s->items[i], cache));
        CHECK(file_exists_with_size(cache, 1948u));
    }
    /* A fresh process wipes leftovers; a running one keeps its table. */
    zip_cache_wipe_if_fresh(s->cache_root);
    CHECK(file_exists_with_size(cache, 1948u));
    memset(g_zip_cache_stamp, 0, sizeof(g_zip_cache_stamp));
    zip_cache_wipe_if_fresh(s->cache_root);
    CHECK(!file_exists_with_size(cache, 1948u));
    free_state(s);
    puts("PASS cache: extraction to <cache>/zip/<hash>/<name>, lazy neighbours, virtual return target, restore selection, ring eviction, fresh-process wipe");
}

static void test_truncation(void) {
    ANativeActivity activity;
    BrowserState* s = new_state(&activity);
    char path[MAX_PATH], file[MAX_PATH], name[32];
    int i;
    unsigned char* data = 0;
    int size = 0;

    /* Archive with 1200 entries: exactly the sorted first 999 files + parent. */
    fixture("archiv-gross.zip/viele", path);
    CHECK(scan_directory(s, path));
    CHECK(s->item_count == MAX_ITEMS);
    CHECK(strcmp(s->message, MAX_ITEMS_TEXT) == 0);
    CHECK(s->items[0].is_parent);
    CHECK(strcmp(s->items[1].name, "V0000.ppm") == 0);
    CHECK(strcmp(s->items[MAX_ITEMS - 1].name, "V0998.ppm") == 0);
    for (i = 2; i < s->item_count; i++) CHECK(name_compare_ci(s->items[i - 1].name, s->items[i].name) < 0);

    /* File-system folder with 1200 files written in reverse order: the
       listing must still be the alphabetical beginning. */
    fixture("viele-dateien", path);
    mkdir(path, 0700);
    CHECK(read_file(g_translation, &data, &size));
    for (i = 1199; i >= 0; i--) {
        FILE* f;
        name[0] = 'D';
        name[1] = (char)('0' + (i / 1000) % 10); name[2] = (char)('0' + (i / 100) % 10);
        name[3] = (char)('0' + (i / 10) % 10); name[4] = (char)('0' + i % 10);
        copy_text(name + 5, ".ppm", 8);
        join_path(file, path, name);
        f = fopen(file, "wb");
        CHECK(f && fwrite(data, 1, (size_t)size, f) == (size_t)size);
        fclose(f);
    }
    free(data);
    CHECK(scan_directory(s, path));
    CHECK(s->item_count == MAX_ITEMS && strcmp(s->message, MAX_ITEMS_TEXT) == 0);
    CHECK(strcmp(s->items[1].name, "D0000.ppm") == 0);
    CHECK(strcmp(s->items[MAX_ITEMS - 1].name, "D0998.ppm") == 0);
    for (i = 2; i < s->item_count; i++) CHECK(name_compare_ci(s->items[i - 1].name, s->items[i].name) < 0);
    remove_directory_tree_shallow(path);
    free_state(s);
    puts("PASS 1000-entry limit: archive and file-system folders keep the sorted beginning, not readdir order");
}

static void test_last_directory(void) {
    ANativeActivity activity;
    BrowserState* s = new_state(&activity);
    char path[MAX_PATH];
    fixture("archiv-deflate.zip/0200", path);
    copy_text(s->last_directory, path, MAX_PATH);
    choose_start_directory(s);
    CHECK(strcmp(s->directory, path) == 0 && s->item_count == 5);
    free_state(s);
    /* A vanished folder falls back to the storage roots without crashing. */
    s = new_state(&activity);
    fixture("weg.zip/0000", path);
    copy_text(s->last_directory, path, MAX_PATH);
    choose_start_directory(s);
    CHECK(strcmp(s->directory, path) != 0 && s->directory[0] == '/');
    free_state(s);
    /* The restore target from the player wins over the remembered folder. */
    s = new_state(&activity);
    fixture("archiv-deflate.zip/0200", path);
    copy_text(s->last_directory, path, MAX_PATH);
    fixture("archiv-deflate.zip/0000", s->restore_directory);
    join_path(s->restore_path, s->restore_directory, "F000003.ppm");
    s->restore_pending = 1;
    choose_start_directory(s);
    CHECK(strcmp(s->directory, s->restore_directory) == 0);
    CHECK(strcmp(s->items[s->selected].name, "F000003.ppm") == 0);
    free_state(s);
    puts("PASS remembered start folder inside an archive, fallback when gone, player return wins");
}

/* Archives beyond 2 GB (v1.4.14): the fixtures are sparse files whose PPM
   entries sit behind a multi-gigabyte hole, so every offset exceeds the old
   31-bit limit (and 32 bits for the ZIP64 variant). */
static void check_big_archive(const char* name, int64_t expected_size, int64_t min_offset) {
    ANativeActivity activity;
    BrowserState* s;
    ZipArchive* zip;
    char path[MAX_PATH], sub[MAX_PATH], zip_path[MAX_PATH], inner[MAX_PATH], cache[MAX_PATH];
    unsigned char* original = 0;
    unsigned char* got = 0;
    unsigned char head[PPM_HEAD_BYTES];
    uint32_t direct[THUMB_PIXELS], from_zip[THUMB_PIXELS];
    int size = 0, i, index;
    BigFile probe;

    fixture(name, path);
    if (!bigfile_open(&probe, path)) {
        printf("  SKIP %s (fixture missing: no sparse file support?)\n", name);
        return;
    }
    CHECK(bigfile_size(&probe) == expected_size);
    bigfile_close(&probe);
    CHECK(zip_split_path(path, zip_path, inner) && strcmp(zip_path, path) == 0);

    zip = zip_archive_open(path);
    CHECK(zip && zip->entry_count == 3 && zip->unsupported == 0 && zip->file_size == expected_size);
    for (i = 0; i < zip->entry_count; i++)
        CHECK(zip->entries[i].locator.local_offset > min_offset);
    /* Full reads with CRC, thumbnails from the header bytes only. */
    CHECK(read_file(g_synthetic, &original, &size) && size == 55282);
    CHECK(decode_thumbnail(g_synthetic, direct));
    for (i = 0; i < 2; i++) {
        const ZipLocator* loc;
        index = find_entry(zip, i == 0 ? "0000/GROSS00.ppm" : "0100/GROSS02.ppm");
        CHECK(index >= 0);
        loc = &zip->entries[index].locator;
        CHECK(loc->method == (i == 0 ? ZIP_METHOD_STORED : ZIP_METHOD_DEFLATE));
        CHECK(zip_read_locator_all(path, loc, &got, &size));
        CHECK(size == 55282 && memcmp(got, original, 55282) == 0);
        free(got);
        got = 0;
        CHECK(zip_read_locator(path, loc, head, PPM_HEAD_BYTES, 0));
        CHECK(decode_thumbnail_bytes(head, PPM_HEAD_BYTES, from_zip));
        CHECK(hash_thumb(from_zip) == hash_thumb(direct));
    }
    zip_archive_free(zip);

    /* Browser listing, previews and extraction through the normal paths. */
    s = new_state(&activity);
    fixture("cache-test", sub);
    mkdir(sub, 0700);
    join_path(s->cache_root, g_fixtures, "cache-test/zip");
    CHECK(scan_directory(s, path) && s->item_count == 3);
    CHECK(strcmp(s->items[1].name, "0000") == 0 && strcmp(s->items[2].name, "0100") == 0);
    join_path(sub, path, "0000");
    CHECK(scan_directory(s, sub) && s->item_count == 3);
    for (i = 0; i < 10; i++) load_one_thumbnail(s, 0, s->item_count - 1);
    index = find_item(s, "GROSS00.ppm");
    CHECK(index >= 0 && s->items[index].thumb_state == 1 && hash_thumb(s->items[index].thumb) == hash_thumb(direct));
    CHECK(browser_item_cache_path(s, &s->items[index], cache));
    CHECK(zip_cache_ensure(cache, s->zip->path, browser_item_locator(s, &s->items[index])));
    CHECK(read_file(cache, &got, &size) && size == 55282 && memcmp(got, original, 55282) == 0);
    free(got);
    free(original);
    free_state(s);
    printf("  %s: %lld bytes, offsets past %lld OK\n", name, (long long)expected_size, (long long)min_offset);
}

static void test_big_archives(void) {
    check_big_archive("archiv-2gb.zip", 2150058439LL, 0x7fffffffLL);
    check_big_archive("archiv-4gb.zip", 4400058591LL, 0xffffffffLL);
    puts("PASS archives beyond 2 GB and 4 GB (ZIP64): index, previews, extraction, listing");
}

/* Random byte damage in archives must only ever produce failures, never
   crashes or reads outside the buffers (verified under the sanitizers). */
static void fuzz_archive(const char* name, int rounds, uint32_t seed) {
    char source[MAX_PATH], target[MAX_PATH];
    unsigned char* data = 0;
    int size = 0, round, opened = 0;
    fixture(name, source);
    fixture("fuzz.zip", target);
    CHECK(read_file(source, &data, &size));
    for (round = 0; round < rounds; round++) {
        unsigned char* copy = (unsigned char*)malloc((size_t)size);
        FILE* f;
        ZipArchive* zip;
        int flips, i;
        memcpy(copy, data, (size_t)size);
        seed = seed * 1664525u + 1013904223u;
        flips = 1 + (int)((seed >> 24) % 12u);
        for (i = 0; i < flips; i++) {
            seed = seed * 1664525u + 1013904223u;
            copy[(seed >> 8) % (uint32_t)size] = (unsigned char)(seed >> 3);
        }
        f = fopen(target, "wb");
        CHECK(f && fwrite(copy, 1, (size_t)size, f) == (size_t)size);
        fclose(f);
        free(copy);
        zip = zip_archive_open(target);
        if (zip) {
            unsigned char* out;
            int entry;
            opened++;
            for (entry = 0; entry < zip->entry_count && entry < 12; entry++) {
                const ZipLocator* loc = &zip->entries[entry].locator;
                int wanted = (int)(loc->uncompressed_size < 400000u ? loc->uncompressed_size : 400000u);
                if (wanted <= 0 || !zip_locator_supported(loc)) continue;
                out = (unsigned char*)malloc((size_t)wanted);
                (void)zip_read_locator(target, loc, out, wanted, wanted == (int)loc->uncompressed_size);
                (void)zip_read_locator(target, loc, out, wanted < PPM_HEAD_BYTES ? wanted : PPM_HEAD_BYTES, 0);
                free(out);
            }
            zip_archive_free(zip);
        }
    }
    unlink(target);
    free(data);
    printf("  %s: %d damaged copies, %d still opened\n", name, rounds, opened);
}

static void test_fuzz(void) {
    fuzz_archive("archiv-deflate.zip", 400, 7u);
    fuzz_archive("daten-l6.zip", 150, 11u);
    fuzz_archive("archiv-zip64.zip", 300, 13u);
    fuzz_archive("archiv-vorspann.zip", 300, 17u);
    puts("PASS damaged archives: no crash, no out-of-bounds access");
}

int main(int argc, char** argv) {
    if (argc != 4) { puts("usage: zip-test FIXTURES SYNTHETIC.ppm TRANSLATION.ppm"); return 2; }
    copy_text(g_fixtures, argv[1], MAX_PATH);
    copy_text(g_synthetic, argv[2], MAX_PATH);
    copy_text(g_translation, argv[3], MAX_PATH);
    test_name_normalization();
    test_split_path();
    test_index();
    test_inflate_roundtrip();
    test_listing();
    test_previews();
    test_cache_and_gallery();
    test_truncation();
    test_last_directory();
    test_big_archives();
    test_fuzz();
    printf("ZIP browser host test: %d checks OK\n", g_checks);
    return 0;
}
