/*
 * ZIP archives as virtual folders (v1.4.13).
 *
 * A regular file whose name ends in ".zip" is treated like a directory:
 * "/sdcard/archiv.zip/0300" lists the PPM entries below "0300/" inside the
 * archive. Only the central directory is read for listings. Static previews
 * inflate just the leading 0x6A0 bytes of an entry; animated previews and the
 * Java player need the complete file, so a selected entry is extracted to the
 * app cache and handed over as an ordinary file.
 *
 * Everything is integer-only C89 that relies solely on the libc declarations
 * in flipbrowser.c, so the same code runs unchanged in the host tests
 * (including AddressSanitizer/UndefinedBehaviorSanitizer). The inflate
 * implementation follows RFC 1951 directly; there is no libz dependency.
 *
 * Archive positions are 64-bit (v1.4.14): the file is read through
 * open/lseek64/read, so archives beyond 2 GB and ZIP64 offsets beyond 4 GB
 * work on 32-bit devices too. Only entry sizes stay 32-bit (a PPM never
 * exceeds 16 MB).
 */
#define ZIP_MAX_ENTRIES 100000
#define ZIP_MAX_NAME_BYTES (8 * 1024 * 1024)
#define ZIP_NAME_LIMIT 512
#define ZIP_EXTRA_LIMIT 1024
#define ZIP_READ_CHUNK 8192
#define ZIP_EOCD_SEARCH (65536 + 22)
#define ZIP_CACHE_RING 16
#define ZIP_METHOD_STORED 0
#define ZIP_METHOD_DEFLATE 8
#define ZIP_FLAG_ENCRYPTED 1
#define ZIP_SIG_LOCAL 0x04034b50u
#define ZIP_SIG_CENTRAL 0x02014b50u
#define ZIP_SIG_EOCD 0x06054b50u
#define ZIP_SIG_EOCD64_LOCATOR 0x07064b50u
#define ZIP_SIG_EOCD64 0x06064b50u

typedef struct ZipLocator {
    int64_t local_offset;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint32_t crc;
    uint16_t method;
    uint16_t flags;
} ZipLocator;

typedef struct ZipEntry {
    ZipLocator locator;
    uint32_t name_offset;
} ZipEntry;

typedef struct ZipArchive {
    char path[MAX_PATH];
    int64_t file_size;
    ZipEntry* entries;
    int entry_count;
    int entry_capacity;
    char* names;
    int names_size;
    int names_capacity;
    int truncated;    /* index limit reached, later entries are not listed */
    int unsupported;  /* encrypted or unknown compression method */
} ZipArchive;

/* ---- Small helpers ------------------------------------------------------ */
static int text_starts_with(const char* text, const char* prefix) {
    while (*prefix) if (*text++ != *prefix++) return 0;
    return 1;
}

static int ends_zip(const char* name) {
    size_t n = strlen(name);
    if (n < 4) return 0;
    return name[n - 4] == '.'
        && ascii_lower((unsigned char)name[n - 3]) == 'z'
        && ascii_lower((unsigned char)name[n - 2]) == 'i'
        && ascii_lower((unsigned char)name[n - 1]) == 'p';
}

static const char* path_basename(const char* path) {
    const char* result = path;
    while (*path) { if (*path == '/') result = path + 1; path++; }
    return result;
}

static int zip_locator_supported(const ZipLocator* loc) {
    if (loc->flags & ZIP_FLAG_ENCRYPTED) return 0;
    return loc->method == ZIP_METHOD_STORED || loc->method == ZIP_METHOD_DEFLATE;
}

/* ---- 64-bit file access -------------------------------------------------
 * stdio's fseek/ftell use 32-bit longs on 32-bit Android, so archives past
 * 2 GB go through the file descriptor API with 64-bit offsets instead. */
#if defined(FLIPBROWSER_HOST_TEST)
#define big_lseek lseek
#define BIG_OPEN_FLAGS O_RDONLY
#else
#define big_lseek lseek64
#if defined(__arm__)
#define BIG_OPEN_FLAGS 0x20000   /* O_RDONLY | O_LARGEFILE on 32-bit ARM */
#elif defined(__i386__)
#define BIG_OPEN_FLAGS 0x8000    /* O_RDONLY | O_LARGEFILE on 32-bit x86 */
#else
#define BIG_OPEN_FLAGS 0         /* 64-bit ABIs need no O_LARGEFILE */
#endif
#endif

typedef struct BigFile {
    int fd;
} BigFile;

static int bigfile_open(BigFile* f, const char* path) {
    f->fd = open(path, BIG_OPEN_FLAGS);
    return f->fd >= 0;
}

static void bigfile_close(BigFile* f) {
    if (f->fd >= 0) close(f->fd);
    f->fd = -1;
}

/* Size in bytes, or -1 when the file cannot be measured. */
static int64_t bigfile_size(BigFile* f) {
    return (int64_t)big_lseek(f->fd, 0, SEEK_END);
}

static int bigfile_read_at(BigFile* f, int64_t offset, unsigned char* out, int size) {
    int done = 0;
    if (offset < 0 || size < 0) return 0;
    if ((int64_t)big_lseek(f->fd, offset, SEEK_SET) != offset) return 0;
    while (done < size) {
        long got = (long)read(f->fd, out + done, (size_t)(size - done));
        if (got <= 0) return 0;
        done += (int)got;
    }
    return 1;
}

static int bigfile_read_next(BigFile* f, unsigned char* out, int size) {
    long got = (long)read(f->fd, out, (size_t)size);
    return got > 0 ? (int)got : 0;
}

/* ---- CRC-32 (IEEE 802.3, as used by ZIP) ------------------------------- */
static uint32_t zip_crc_update(uint32_t crc, const unsigned char* data, int size) {
    int i, bit;
    crc = ~crc;
    for (i = 0; i < size; i++) {
        crc ^= data[i];
        for (bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

/* ---- Bounded, buffered file reader ------------------------------------- */
typedef struct ZipReader {
    BigFile file;
    unsigned char* buffer;
    int size;
    int pos;
    int64_t remaining;
    int error;
} ZipReader;

static void zip_reader_close(ZipReader* r) {
    bigfile_close(&r->file);
    if (r->buffer) free(r->buffer);
    r->buffer = 0;
}

static int zip_reader_open(ZipReader* r, const char* path, int64_t offset, int64_t length) {
    memset(r, 0, sizeof(*r));
    r->file.fd = -1;
    if (offset < 0 || length < 0) return 0;
    r->buffer = (unsigned char*)malloc(ZIP_READ_CHUNK);
    if (!r->buffer) return 0;
    if (!bigfile_open(&r->file, path)
            || (int64_t)big_lseek(r->file.fd, offset, SEEK_SET) != offset) {
        zip_reader_close(r);
        return 0;
    }
    r->remaining = length;
    return 1;
}

static int zip_reader_fill(ZipReader* r) {
    int want;
    if (r->error || r->remaining <= 0) return 0;
    want = r->remaining < ZIP_READ_CHUNK ? (int)r->remaining : ZIP_READ_CHUNK;
    r->size = bigfile_read_next(&r->file, r->buffer, want);
    r->pos = 0;
    if (r->size <= 0) { r->error = 1; r->size = 0; return 0; }
    r->remaining -= r->size;
    return 1;
}

static int zip_reader_byte(ZipReader* r) {
    if (r->pos >= r->size && !zip_reader_fill(r)) return -1;
    return r->buffer[r->pos++];
}

static int zip_reader_read(ZipReader* r, unsigned char* out, int n) {
    int done = 0;
    while (done < n) {
        int chunk;
        if (r->pos >= r->size && !zip_reader_fill(r)) return 0;
        chunk = r->size - r->pos;
        if (chunk > n - done) chunk = n - done;
        memcpy(out + done, r->buffer + r->pos, (size_t)chunk);
        r->pos += chunk;
        done += chunk;
    }
    return 1;
}

static int zip_reader_skip(ZipReader* r, int64_t n) {
    while (n > 0) {
        int64_t chunk;
        if (r->pos >= r->size && !zip_reader_fill(r)) return 0;
        chunk = r->size - r->pos;
        if (chunk > n) chunk = n;
        r->pos += (int)chunk;
        n -= chunk;
    }
    return 1;
}

/* ---- Inflate (RFC 1951) with early stop ---------------------------------
 * The output buffer keeps everything produced so far and therefore doubles
 * as the 32 KB sliding window. Decoding stops as soon as `limit` bytes exist,
 * which lets previews read only the PPM header and thumbnail. */
#define INF_MAXBITS 15
#define INF_MAXLCODES 286
#define INF_MAXDCODES 30
#define INF_MAXCODES (INF_MAXLCODES + INF_MAXDCODES)
#define INF_FIXLCODES 288
#define INF_FAST_BITS 9
#define INF_FAST_SIZE (1 << INF_FAST_BITS)

/* Canonical code as in RFC 1951 plus a 9-bit lookup table for the common
   short codes; longer codes fall back to the bit-by-bit canonical decode. */
typedef struct InflateHuffman {
    short count[INF_MAXBITS + 1];
    short symbol[INF_FIXLCODES];
    uint16_t fast[INF_FAST_SIZE]; /* (length << 12) | symbol, 0 = not in table */
} InflateHuffman;

typedef struct Inflater {
    ZipReader* in;
    unsigned char* out;
    int limit;
    int out_pos;
    uint32_t bitbuf;
    int bitcnt;
    int done;
} Inflater;

static int inflate_bits(Inflater* s, int need, int* out) {
    uint32_t val = s->bitbuf;
    while (s->bitcnt < need) {
        int byte = zip_reader_byte(s->in);
        if (byte < 0) return 0;
        val |= (uint32_t)byte << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    *out = (int)(val & ((1u << need) - 1u));
    return 1;
}

static int inflate_decode(Inflater* s, const InflateHuffman* h, int* symbol) {
    int len, code = 0, first = 0, index = 0, count, bit;
    /* Fast path: peek INF_FAST_BITS bits. Near the end of the compressed
       data fewer bits may be available, which the slow path handles. */
    while (s->bitcnt < INF_FAST_BITS) {
        int byte = zip_reader_byte(s->in);
        if (byte < 0) break;
        s->bitbuf |= (uint32_t)byte << s->bitcnt;
        s->bitcnt += 8;
    }
    if (s->bitcnt >= INF_FAST_BITS) {
        unsigned int entry = h->fast[s->bitbuf & (INF_FAST_SIZE - 1)];
        if (entry) {
            len = (int)(entry >> 12);
            s->bitbuf >>= len;
            s->bitcnt -= len;
            *symbol = (int)(entry & 0xfffu);
            return 1;
        }
    }
    for (len = 1; len <= INF_MAXBITS; len++) {
        if (!inflate_bits(s, 1, &bit)) return 0;
        code |= bit;
        count = h->count[len];
        if (code - count < first) {
            *symbol = h->symbol[index + (code - first)];
            return 1;
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return 0;
}

/* Fills the lookup table with every code of at most INF_FAST_BITS bits.
   Deflate stores codes most significant bit first while the stream is read
   least significant bit first, so each code is bit-reversed for indexing. */
static void inflate_build_fast(InflateHuffman* h, const short* length, int n) {
    int next_code[INF_MAXBITS + 1];
    int len, symbol, code = 0;
    memset(h->fast, 0, sizeof(h->fast));
    for (len = 1; len <= INF_MAXBITS; len++) {
        code = (code + (len > 1 ? h->count[len - 1] : 0)) << 1;
        next_code[len] = code;
    }
    for (symbol = 0; symbol < n; symbol++) {
        int reversed = 0, i, bit;
        len = length[symbol];
        if (len == 0) continue;
        code = next_code[len]++;
        if (len > INF_FAST_BITS) continue;
        for (bit = 0; bit < len; bit++)
            reversed |= ((code >> bit) & 1) << (len - 1 - bit);
        for (i = reversed; i < INF_FAST_SIZE; i += 1 << len)
            h->fast[i] = (uint16_t)((len << 12) | symbol);
    }
}

/* Returns 0 for a complete code, > 0 for an incomplete one, < 0 when the
   lengths are over-subscribed. */
static int inflate_construct(InflateHuffman* h, const short* length, int n) {
    int symbol, len, left;
    short offs[INF_MAXBITS + 1];
    for (len = 0; len <= INF_MAXBITS; len++) h->count[len] = 0;
    for (symbol = 0; symbol < n; symbol++) h->count[length[symbol]]++;
    memset(h->fast, 0, sizeof(h->fast));
    if (h->count[0] == n) return 0;
    left = 1;
    for (len = 1; len <= INF_MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;
    }
    offs[1] = 0;
    for (len = 1; len < INF_MAXBITS; len++) offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (symbol = 0; symbol < n; symbol++)
        if (length[symbol] != 0) h->symbol[offs[length[symbol]]++] = (short)symbol;
    inflate_build_fast(h, length, n);
    return left;
}

static int inflate_codes(Inflater* s, const InflateHuffman* lencode,
                         const InflateHuffman* distcode) {
    static const short lbase[29] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
    static const short lext[29] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
    static const short dbase[30] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
        8193, 12289, 16385, 24577 };
    static const short dext[30] = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
    int symbol, len, dist, extra;
    for (;;) {
        if (s->out_pos >= s->limit) { s->done = 1; return 1; }
        if (!inflate_decode(s, lencode, &symbol)) return 0;
        if (symbol < 256) {
            s->out[s->out_pos++] = (unsigned char)symbol;
        } else if (symbol == 256) {
            return 1;
        } else {
            symbol -= 257;
            if (symbol >= 29) return 0;
            if (!inflate_bits(s, lext[symbol], &extra)) return 0;
            len = lbase[symbol] + extra;
            if (!inflate_decode(s, distcode, &symbol)) return 0;
            if (symbol >= 30) return 0;
            if (!inflate_bits(s, dext[symbol], &extra)) return 0;
            dist = dbase[symbol] + extra;
            if (dist > s->out_pos) return 0;
            if (len > s->limit - s->out_pos) len = s->limit - s->out_pos;
            while (len-- > 0) {
                s->out[s->out_pos] = s->out[s->out_pos - dist];
                s->out_pos++;
            }
        }
    }
}

static int inflate_stored(Inflater* s) {
    int len, nlen, b0, b1, b2, b3;
    s->bitbuf = 0;
    s->bitcnt = 0;
    b0 = zip_reader_byte(s->in); b1 = zip_reader_byte(s->in);
    b2 = zip_reader_byte(s->in); b3 = zip_reader_byte(s->in);
    if (b0 < 0 || b1 < 0 || b2 < 0 || b3 < 0) return 0;
    len = b0 | (b1 << 8);
    nlen = b2 | (b3 << 8);
    if (len != (~nlen & 0xffff)) return 0;
    if (len > s->limit - s->out_pos) {
        len = s->limit - s->out_pos;
        s->done = 1;
    }
    if (len > 0 && !zip_reader_read(s->in, s->out + s->out_pos, len)) return 0;
    s->out_pos += len;
    return 1;
}

static int inflate_fixed(Inflater* s) {
    InflateHuffman* lencode;
    InflateHuffman* distcode;
    short lengths[INF_FIXLCODES];
    int symbol, ok;
    lencode = (InflateHuffman*)malloc(sizeof(InflateHuffman) * 2);
    if (!lencode) return 0;
    distcode = lencode + 1;
    for (symbol = 0; symbol < 144; symbol++) lengths[symbol] = 8;
    for (; symbol < 256; symbol++) lengths[symbol] = 9;
    for (; symbol < 280; symbol++) lengths[symbol] = 7;
    for (; symbol < INF_FIXLCODES; symbol++) lengths[symbol] = 8;
    inflate_construct(lencode, lengths, INF_FIXLCODES);
    for (symbol = 0; symbol < INF_MAXDCODES; symbol++) lengths[symbol] = 5;
    inflate_construct(distcode, lengths, INF_MAXDCODES);
    ok = inflate_codes(s, lencode, distcode);
    free(lencode);
    return ok;
}

static int inflate_dynamic(Inflater* s) {
    static const short order[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    InflateHuffman* lencode;
    InflateHuffman* distcode;
    short lengths[INF_MAXCODES];
    int nlen, ndist, ncode, index, err, value, ok = 0;

    if (!inflate_bits(s, 5, &nlen) || !inflate_bits(s, 5, &ndist)
            || !inflate_bits(s, 4, &ncode)) return 0;
    nlen += 257; ndist += 1; ncode += 4;
    if (nlen > INF_MAXLCODES || ndist > INF_MAXDCODES) return 0;
    lencode = (InflateHuffman*)malloc(sizeof(InflateHuffman) * 2);
    if (!lencode) return 0;
    distcode = lencode + 1;

    for (index = 0; index < ncode; index++) {
        if (!inflate_bits(s, 3, &value)) goto done;
        lengths[order[index]] = (short)value;
    }
    for (; index < 19; index++) lengths[order[index]] = 0;
    if (inflate_construct(lencode, lengths, 19) != 0) goto done;

    index = 0;
    while (index < nlen + ndist) {
        int symbol, len;
        if (!inflate_decode(s, lencode, &symbol)) goto done;
        if (symbol < 16) {
            lengths[index++] = (short)symbol;
        } else {
            len = 0;
            if (symbol == 16) {
                if (index == 0) goto done;
                len = lengths[index - 1];
                if (!inflate_bits(s, 2, &value)) goto done;
                symbol = 3 + value;
            } else if (symbol == 17) {
                if (!inflate_bits(s, 3, &value)) goto done;
                symbol = 3 + value;
            } else {
                if (!inflate_bits(s, 7, &value)) goto done;
                symbol = 11 + value;
            }
            if (index + symbol > nlen + ndist) goto done;
            while (symbol--) lengths[index++] = (short)len;
        }
    }
    if (lengths[256] == 0) goto done;
    err = inflate_construct(lencode, lengths, nlen);
    if (err && (err < 0 || nlen != lencode->count[0] + lencode->count[1])) goto done;
    err = inflate_construct(distcode, lengths + nlen, ndist);
    if (err && (err < 0 || ndist != distcode->count[0] + distcode->count[1])) goto done;
    ok = inflate_codes(s, lencode, distcode);
done:
    free(lencode);
    return ok;
}

/* Inflates until `limit` output bytes exist or the stream ends. Returns the
   number of bytes produced, or -1 on any error. */
static int zip_inflate(ZipReader* in, unsigned char* out, int limit) {
    Inflater s;
    int last, type;
    memset(&s, 0, sizeof(s));
    s.in = in;
    s.out = out;
    s.limit = limit;
    if (limit <= 0) return 0;
    do {
        if (!inflate_bits(&s, 1, &last) || !inflate_bits(&s, 2, &type)) return -1;
        if (type == 0) { if (!inflate_stored(&s)) return -1; }
        else if (type == 1) { if (!inflate_fixed(&s)) return -1; }
        else if (type == 2) { if (!inflate_dynamic(&s)) return -1; }
        else return -1;
        if (s.done || s.out_pos >= limit) break;
    } while (!last);
    return s.out_pos;
}

/* ---- Central directory index ------------------------------------------- */
static void zip_archive_free(ZipArchive* zip) {
    if (!zip) return;
    if (zip->entries) free(zip->entries);
    if (zip->names) free(zip->names);
    free(zip);
}

static int zip_grow(void** block, int* capacity, int needed, int element, int limit) {
    int next = *capacity ? *capacity : 256;
    void* fresh;
    if (needed <= *capacity) return 1;
    while (next < needed) next *= 2;
    if (next > limit) next = limit;
    if (next < needed) return 0;
    fresh = malloc((size_t)next * (size_t)element);
    if (!fresh) return 0;
    if (*block) {
        memcpy(fresh, *block, (size_t)(*capacity) * (size_t)element);
        free(*block);
    }
    *block = fresh;
    *capacity = next;
    return 1;
}

/* Cleans an entry name in place. Returns 0 when the entry is a directory,
   unsafe, not a PPM file or otherwise not worth listing. */
static int zip_normalize_name(char* name) {
    int i, n, out = 0;
    for (i = 0; name[i]; i++) if (name[i] == '\\') name[i] = '/';
    n = i;
    i = 0;
    for (;;) {
        if (name[i] == '/') { i++; continue; }
        if (name[i] == '.' && name[i + 1] == '/') { i += 2; continue; }
        break;
    }
    while (i < n) {
        if (name[i] == '/' && (out == 0 || name[out - 1] == '/')) { i++; continue; }
        name[out++] = name[i++];
    }
    name[out] = 0;
    if (out == 0 || name[out - 1] == '/') return 0;
    for (i = 0; i < out; i++) {
        /* Reject "." and ".." path components anywhere in the name. */
        if (name[i] == '.' && (i == 0 || name[i - 1] == '/')) {
            if (name[i + 1] == 0 || name[i + 1] == '/') return 0;
            if (name[i + 1] == '.' && (name[i + 2] == 0 || name[i + 2] == '/')) return 0;
        }
    }
    return ends_ppm(name);
}

static int zip_probe_signature(BigFile* f, int64_t offset, uint32_t expected) {
    unsigned char sig[4];
    uint32_t value;
    if (!bigfile_read_at(f, offset, sig, 4)) return 0;
    if (!read_u32le_at(sig, 4, 0, &value)) return 0;
    return value == expected;
}

/* Reads one 64-bit little-endian value as a non-negative int64. */
static int zip_read_i64(const unsigned char* data, int size, int offset, int64_t* out) {
    uint32_t lo, hi;
    if (!read_u32le_at(data, size, offset, &lo) || !read_u32le_at(data, size, offset + 4, &hi)) return 0;
    if (hi & 0x80000000u) return 0;
    *out = ((int64_t)hi << 32) | (int64_t)lo;
    return 1;
}

/* Locates the central directory. Handles ZIP64 records for large entry
   counts and offsets as well as archives with prepended data
   (self-extractors). `delta` is the amount by which every stored offset has
   to be shifted (0 for ordinary archives). */
static int zip_locate_central(BigFile* f, int64_t file_size, int64_t* cd_start,
                              int64_t* cd_size, int* entry_total, int64_t* delta) {
    unsigned char* tail;
    int tail_len, i, found = -1, comment_len;
    uint32_t sig, size32, offset32;
    int entries16;
    int64_t cd_offset, cd_end, alt, value;

    tail_len = file_size < ZIP_EOCD_SEARCH ? (int)file_size : ZIP_EOCD_SEARCH;
    if (tail_len < 22) return 0;
    tail = (unsigned char*)malloc((size_t)tail_len);
    if (!tail) return 0;
    if (!bigfile_read_at(f, file_size - tail_len, tail, tail_len)) { free(tail); return 0; }
    for (i = tail_len - 22; i >= 0; i--) {
        if (!read_u32le_at(tail, tail_len, i, &sig) || sig != ZIP_SIG_EOCD) continue;
        read_u16le_at(tail, tail_len, i + 20, &comment_len);
        if (i + 22 + comment_len == tail_len) { found = i; break; }
        if (found < 0) found = i;
    }
    if (found < 0) { free(tail); return 0; }
    read_u16le_at(tail, tail_len, found + 10, &entries16);
    read_u32le_at(tail, tail_len, found + 12, &size32);
    read_u32le_at(tail, tail_len, found + 16, &offset32);
    cd_end = (file_size - tail_len) + found;
    free(tail);

    if (entries16 == 0xffff || size32 == 0xffffffffu || offset32 == 0xffffffffu) {
        unsigned char locator[20];
        unsigned char record[56];
        int64_t record_pos;
        if (!bigfile_read_at(f, cd_end - 20, locator, 20)) return 0;
        if (!read_u32le_at(locator, 20, 0, &sig) || sig != ZIP_SIG_EOCD64_LOCATOR) return 0;
        if (!zip_read_i64(locator, 20, 8, &record_pos) || record_pos >= cd_end) return 0;
        if (!bigfile_read_at(f, record_pos, record, 56)) return 0;
        if (!read_u32le_at(record, 56, 0, &sig) || sig != ZIP_SIG_EOCD64) return 0;
        if (!zip_read_i64(record, 56, 32, &value) || value > 0x7fffffff) return 0;
        *entry_total = (int)value;
        if (!zip_read_i64(record, 56, 40, cd_size)) return 0;
        if (!zip_read_i64(record, 56, 48, &cd_offset)) return 0;
        cd_end = record_pos;
    } else {
        *entry_total = entries16;
        *cd_size = (int64_t)size32;
        cd_offset = (int64_t)offset32;
    }
    if (*cd_size < 0 || *cd_size > cd_end) return 0;
    *delta = 0;
    if (cd_offset <= cd_end - *cd_size && zip_probe_signature(f, cd_offset, ZIP_SIG_CENTRAL)) {
        *cd_start = cd_offset;
        return 1;
    }
    alt = cd_end - *cd_size;
    if (zip_probe_signature(f, alt, ZIP_SIG_CENTRAL)) {
        *cd_start = alt;
        *delta = alt - cd_offset;
        return 1;
    }
    if (*cd_size == 0 && *entry_total == 0) { *cd_start = alt; return 1; }
    return 0;
}

/* Resolves 0xFFFFFFFF placeholders from the ZIP64 extra field (id 0x0001).
   The field only carries the values that are placeholders, in this order.
   Entry sizes must still fit 32 bits (PPM files are small); the local
   header offset may be anywhere in a 63-bit file. */
static int zip_parse_extra_zip64(const unsigned char* extra, int extra_len,
                                 uint32_t* usize, uint32_t* csize, int64_t* offset) {
    int pos = 0;
    while (pos + 4 <= extra_len) {
        int id, size, p, end;
        int64_t value;
        read_u16le_at(extra, extra_len, pos, &id);
        read_u16le_at(extra, extra_len, pos + 2, &size);
        p = pos + 4;
        end = p + size;
        if (end > extra_len) return 0;
        if (id == 0x0001) {
            if (*usize == 0xffffffffu) {
                if (!zip_read_i64(extra, end, p, &value) || value >= 0xffffffff) return 0;
                *usize = (uint32_t)value;
                p += 8;
            }
            if (*csize == 0xffffffffu) {
                if (!zip_read_i64(extra, end, p, &value) || value >= 0xffffffff) return 0;
                *csize = (uint32_t)value;
                p += 8;
            }
            if (*offset == (int64_t)0xffffffff) {
                if (!zip_read_i64(extra, end, p, offset)) return 0;
                p += 8;
            }
            return 1;
        }
        pos = end;
    }
    return 0;
}

static ZipArchive* zip_archive_open(const char* path) {
    ZipArchive* zip;
    BigFile f;
    ZipReader reader;
    unsigned char header[46];
    unsigned char* extra;
    char* name;
    int64_t cd_start = 0, cd_size = 0, delta = 0, file_size, offset;
    int entry_total = 0, index, name_len, extra_len, comment_len, method, flags;
    uint32_t sig, crc, csize, usize, offset32;

    if (!bigfile_open(&f, path)) return 0;
    file_size = bigfile_size(&f);
    if (file_size < 22) { bigfile_close(&f); return 0; }
    zip = (ZipArchive*)malloc(sizeof(ZipArchive));
    if (!zip) { bigfile_close(&f); return 0; }
    memset(zip, 0, sizeof(*zip));
    copy_text(zip->path, path, MAX_PATH);
    zip->file_size = file_size;
    if (!zip_locate_central(&f, file_size, &cd_start, &cd_size, &entry_total, &delta)) {
        bigfile_close(&f);
        zip_archive_free(zip);
        return 0;
    }
    bigfile_close(&f);
    name = (char*)malloc(ZIP_NAME_LIMIT + 1);
    extra = (unsigned char*)malloc(ZIP_EXTRA_LIMIT);
    if (!name || !extra || !zip_reader_open(&reader, path, cd_start, cd_size)) {
        if (name) free(name);
        if (extra) free(extra);
        zip_archive_free(zip);
        return 0;
    }

    for (index = 0; index < entry_total; index++) {
        ZipEntry entry;
        int keep;
        if (!zip_reader_read(&reader, header, 46)) break;
        if (!read_u32le_at(header, 46, 0, &sig) || sig != ZIP_SIG_CENTRAL) break;
        read_u16le_at(header, 46, 8, &flags);
        read_u16le_at(header, 46, 10, &method);
        read_u32le_at(header, 46, 16, &crc);
        read_u32le_at(header, 46, 20, &csize);
        read_u32le_at(header, 46, 24, &usize);
        read_u16le_at(header, 46, 28, &name_len);
        read_u16le_at(header, 46, 30, &extra_len);
        read_u16le_at(header, 46, 32, &comment_len);
        read_u32le_at(header, 46, 42, &offset32);
        offset = (int64_t)offset32;
        if (name_len > ZIP_NAME_LIMIT) {
            if (!zip_reader_skip(&reader, (int64_t)name_len + extra_len + comment_len)) break;
            continue;
        }
        if (!zip_reader_read(&reader, (unsigned char*)name, name_len)) break;
        name[name_len] = 0;
        if (extra_len > ZIP_EXTRA_LIMIT) {
            if (!zip_reader_skip(&reader, extra_len)) break;
            extra_len = 0;
        } else if (extra_len > 0 && !zip_reader_read(&reader, extra, extra_len)) break;
        if (comment_len > 0 && !zip_reader_skip(&reader, comment_len)) break;
        keep = zip_normalize_name(name);
        if (!keep) continue;
        if (usize == 0xffffffffu || csize == 0xffffffffu || offset32 == 0xffffffffu) {
            if (!zip_parse_extra_zip64(extra, extra_len, &usize, &csize, &offset)) {
                zip->unsupported++;
                continue;
            }
        }
        if (usize > 0x7fffffffu || csize > 0x7fffffffu
                || offset + delta < 0 || offset + delta > file_size) {
            zip->unsupported++;
            continue;
        }
        if (zip->entry_count >= ZIP_MAX_ENTRIES
                || zip->names_size + name_len + 1 > ZIP_MAX_NAME_BYTES) {
            zip->truncated = 1;
            break;
        }
        if (!zip_grow((void**)&zip->entries, &zip->entry_capacity, zip->entry_count + 1,
                      (int)sizeof(ZipEntry), ZIP_MAX_ENTRIES)
                || !zip_grow((void**)&zip->names, &zip->names_capacity,
                             zip->names_size + name_len + 1, 1, ZIP_MAX_NAME_BYTES)) {
            zip->truncated = 1;
            break;
        }
        entry.locator.local_offset = offset + delta;
        entry.locator.compressed_size = csize;
        entry.locator.uncompressed_size = usize;
        entry.locator.crc = crc;
        entry.locator.method = (uint16_t)method;
        entry.locator.flags = (uint16_t)flags;
        entry.name_offset = (uint32_t)zip->names_size;
        if (!zip_locator_supported(&entry.locator)) zip->unsupported++;
        memcpy(zip->names + zip->names_size, name, (size_t)name_len + 1);
        zip->names_size += name_len + 1;
        zip->entries[zip->entry_count++] = entry;
    }
    zip_reader_close(&reader);
    free(name);
    free(extra);
    return zip;
}

static const char* zip_entry_name(const ZipArchive* zip, int index) {
    return zip->names + zip->entries[index].name_offset;
}

/* ---- Entry data --------------------------------------------------------- */
/* Reads the first `wanted` uncompressed bytes of an entry. With verify_crc
   set, wanted must equal the uncompressed size and the CRC is checked.
   Returns 1 on success. */
static int zip_read_locator(const char* zip_path, const ZipLocator* loc,
                            unsigned char* out, int wanted, int verify_crc) {
    BigFile f;
    unsigned char local[30];
    int name_len, extra_len, produced;
    uint32_t sig;
    int64_t file_size, data_offset;
    if (!zip_locator_supported(loc)) return 0;
    if (wanted <= 0 || (uint32_t)wanted > loc->uncompressed_size) return 0;
    if (verify_crc && (uint32_t)wanted != loc->uncompressed_size) return 0;
    if (!bigfile_open(&f, zip_path)) return 0;
    file_size = bigfile_size(&f);
    if (file_size <= 0 || !bigfile_read_at(&f, loc->local_offset, local, 30)
            || !read_u32le_at(local, 30, 0, &sig) || sig != ZIP_SIG_LOCAL) {
        bigfile_close(&f);
        return 0;
    }
    read_u16le_at(local, 30, 26, &name_len);
    read_u16le_at(local, 30, 28, &extra_len);
    data_offset = loc->local_offset + 30 + name_len + extra_len;
    if (data_offset > file_size
            || (int64_t)loc->compressed_size > file_size - data_offset) {
        bigfile_close(&f);
        return 0;
    }
    if (loc->method == ZIP_METHOD_STORED) {
        if ((uint32_t)wanted > loc->compressed_size
                || !bigfile_read_at(&f, data_offset, out, wanted)) {
            bigfile_close(&f);
            return 0;
        }
        bigfile_close(&f);
    } else {
        ZipReader reader;
        bigfile_close(&f);
        if (!zip_reader_open(&reader, zip_path, data_offset,
                             (int64_t)loc->compressed_size)) return 0;
        produced = zip_inflate(&reader, out, wanted);
        zip_reader_close(&reader);
        if (produced != wanted) return 0;
    }
    if (verify_crc && zip_crc_update(0, out, wanted) != loc->crc) return 0;
    return 1;
}

/* Reads the complete entry into a fresh buffer (caller frees). */
static int zip_read_locator_all(const char* zip_path, const ZipLocator* loc,
                                unsigned char** out_data, int* out_size) {
    unsigned char* data;
    int size = (int)loc->uncompressed_size;
    if (loc->uncompressed_size > PPM_MAX_PREVIEW_BYTES || size < PPM_MIN_BYTES) return 0;
    data = (unsigned char*)malloc((size_t)size);
    if (!data) return 0;
    if (!zip_read_locator(zip_path, loc, data, size, 1)) { free(data); return 0; }
    *out_data = data;
    *out_size = size;
    return 1;
}

static int zip_write_file(const char* path, const unsigned char* data, int size) {
    FILE* f = fopen(path, "wb");
    size_t written;
    if (!f) return 0;
    written = size > 0 ? fwrite(data, 1, (size_t)size, f) : 0;
    if (fclose(f) != 0) return 0;
    return written == (size_t)size;
}

static void make_directory(const char* path) {
    (void)mkdir(path, 0700);
}

/* Extracts an entry to `dest`, writing a temporary file first so a crash
   never leaves a truncated PPM behind. Parent directories are created. */
static int zip_extract_locator(const char* zip_path, const ZipLocator* loc, const char* dest) {
    unsigned char* data = 0;
    int size = 0, ok;
    char temp[MAX_PATH];
    char parent[MAX_PATH];
    char grand[MAX_PATH];
    if (strlen(dest) + 6 >= MAX_PATH) return 0;
    if (!zip_read_locator_all(zip_path, loc, &data, &size)) return 0;
    parent_path(dest, parent);
    parent_path(parent, grand);
    make_directory(grand);
    make_directory(parent);
    copy_text(temp, dest, MAX_PATH);
    copy_text(temp + strlen(dest), ".part", MAX_PATH - (int)strlen(dest));
    ok = zip_write_file(temp, data, size);
    free(data);
    if (ok && rename(temp, dest) != 0) ok = 0;
    if (!ok) (void)unlink(temp);
    return ok;
}

/* ---- Extraction cache ---------------------------------------------------
 * The Java player only opens ordinary files, so selected entries are copied
 * to <cache>/zip/<hash>/<name>. A small process-wide least-recently-used
 * table keeps the current Flipnote and its swipe neighbours and deletes the
 * copies that were not touched for the longest time. */
static volatile int g_zip_cache_lock;
static char g_zip_cache_ring[ZIP_CACHE_RING][MAX_PATH];
static uint32_t g_zip_cache_stamp[ZIP_CACHE_RING]; /* 0 = free slot */
static uint32_t g_zip_cache_clock;

static void zip_cache_lock(void) { while (try_lock(&g_zip_cache_lock)) usleep(1000); }
static void zip_cache_unlock(void) { release_lock(&g_zip_cache_lock); }

static uint32_t zip_hash_text(uint32_t hash, const char* text) {
    while (*text) { hash ^= (unsigned char)*text++; hash *= 16777619u; }
    return hash;
}

static void hex8(uint32_t value, char* out) {
    static const char digits[] = "0123456789abcdef";
    int i;
    for (i = 7; i >= 0; i--) { out[i] = digits[value & 15u]; value >>= 4; }
    out[8] = 0;
}

/* Cache file for an entry: <root>/<hash of archive + folder>/<basename>. */
static int zip_cache_path(const char* cache_root, const char* zip_path,
                          const char* inner, const char* base, char* out) {
    char hash[9];
    char dir[MAX_PATH];
    uint32_t h = zip_hash_text(2166136261u, zip_path);
    h = zip_hash_text(h, "\n");
    h = zip_hash_text(h, inner);
    hex8(h, hash);
    if (!cache_root[0] || !base[0]) return 0;
    if (strlen(cache_root) + 1 + 8 + 1 + strlen(base) + 6 >= MAX_PATH) return 0;
    join_path(dir, cache_root, hash);
    join_path(out, dir, base);
    return 1;
}

static int file_exists_with_size(const char* path, uint32_t size) {
    int actual = file_size32(path);
    return actual >= 0 && (uint32_t)actual == size;
}

/* Marks a cache copy as just used. A copy that is not in the table yet
   replaces the free or least recently used slot, whose file is deleted. The
   Flipnote being played is always the most recent entry, so loading its two
   swipe neighbours can never evict it. */
static void zip_cache_remember(const char* path) {
    int i, victim = -1;
    zip_cache_lock();
    g_zip_cache_clock++;
    if (g_zip_cache_clock == 0) g_zip_cache_clock = 1;
    for (i = 0; i < ZIP_CACHE_RING; i++) {
        if (g_zip_cache_stamp[i] && strcmp(g_zip_cache_ring[i], path) == 0) {
            g_zip_cache_stamp[i] = g_zip_cache_clock;
            zip_cache_unlock();
            return;
        }
        if (victim < 0 || g_zip_cache_stamp[i] < g_zip_cache_stamp[victim]) victim = i;
    }
    if (g_zip_cache_stamp[victim]) (void)unlink(g_zip_cache_ring[victim]);
    copy_text(g_zip_cache_ring[victim], path, MAX_PATH);
    g_zip_cache_stamp[victim] = g_zip_cache_clock;
    zip_cache_unlock();
}

static int zip_cache_is_empty_locked(void) {
    int i;
    for (i = 0; i < ZIP_CACHE_RING; i++) if (g_zip_cache_stamp[i]) return 0;
    return 1;
}

/* Makes sure the extracted copy exists and is complete. */
static int zip_cache_ensure(const char* cache_path, const char* zip_path, const ZipLocator* loc) {
    if (!cache_path[0]) return 0;
    if (!file_exists_with_size(cache_path, loc->uncompressed_size)) {
        if (!zip_extract_locator(zip_path, loc, cache_path)) return 0;
    }
    zip_cache_remember(cache_path);
    return 1;
}

static void remove_directory_tree_shallow(const char* dir) {
    DIR* d = opendir(dir);
    struct dirent* ent;
    char full[MAX_PATH];
    if (!d) return;
    while ((ent = readdir(d)) != 0) {
        if (ent->d_name[0] == '.' && (ent->d_name[1] == 0
                || (ent->d_name[1] == '.' && ent->d_name[2] == 0))) continue;
        join_path(full, dir, ent->d_name);
        (void)unlink(full);
    }
    closedir(d);
    (void)rmdir(dir);
}

/* Deletes leftovers from earlier runs. Only called while no extraction of
   this process is in use, i.e. when the ring is still empty. */
static void zip_cache_wipe_if_fresh(const char* cache_root) {
    DIR* d;
    struct dirent* ent;
    char full[MAX_PATH];
    if (!cache_root[0]) return;
    zip_cache_lock();
    if (!zip_cache_is_empty_locked()) { zip_cache_unlock(); return; }
    d = opendir(cache_root);
    if (d) {
        while ((ent = readdir(d)) != 0) {
            DIR* test;
            if (ent->d_name[0] == '.' && (ent->d_name[1] == 0
                    || (ent->d_name[1] == '.' && ent->d_name[2] == 0))) continue;
            join_path(full, cache_root, ent->d_name);
            test = opendir(full);
            if (test) { closedir(test); remove_directory_tree_shallow(full); }
            else (void)unlink(full);
        }
        closedir(d);
    }
    zip_cache_unlock();
}

/* ---- Virtual paths ------------------------------------------------------ */
/* Splits "/sdcard/a.zip/0300" into "/sdcard/a.zip" and "0300". A directory
   that merely happens to be called something.zip stays a directory. */
static int zip_split_path(const char* path, char* zip_out, char* inner_out) {
    int i, n = (int)strlen(path);
    for (i = 4; i <= n; i++) {
        if (path[i] != 0 && path[i] != '/') continue;
        if (path[i - 4] == '.'
                && ascii_lower((unsigned char)path[i - 3]) == 'z'
                && ascii_lower((unsigned char)path[i - 2]) == 'i'
                && ascii_lower((unsigned char)path[i - 1]) == 'p') {
            char candidate[MAX_PATH];
            DIR* d;
            BigFile f;
            int j = 0, k;
            if (i >= MAX_PATH) return 0;
            memcpy(candidate, path, (size_t)i);
            candidate[i] = 0;
            d = opendir(candidate);
            if (d) { closedir(d); continue; }
            if (!bigfile_open(&f, candidate)) continue;
            bigfile_close(&f);
            copy_text(zip_out, candidate, MAX_PATH);
            k = i;
            while (path[k] == '/') k++;
            while (path[k] && j < MAX_PATH - 1) inner_out[j++] = path[k++];
            while (j > 0 && inner_out[j - 1] == '/') j--;
            inner_out[j] = 0;
            return 1;
        }
    }
    return 0;
}
