/* Pool-free deterministic output probe. This compares modes; it is not a proof
 * of the original algorithm's correctness. Build against either library. */
#include "../src/pearl_pipeline.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#ifndef PEARL_PROBE_K
#define PEARL_PROBE_K 4096
#endif
extern "C" {
int pearl_init(int, int);
void pearl_set_b(const int8_t*, int);
void pearl_use_b(int);
void pearl_set_nhi(int);
void pearl_set_key_target(const uint32_t*, const uint32_t*);
void pearl_strip_submit(const int8_t*);
void pearl_strip_submit_fast(const int8_t*);
const uint32_t* pearl_strip_wait_ptr();
void pearl_strip_wait(uint32_t*);
void pearl_strip_wait_done();
int pearl_get_hit_result(int*, int*);
void* pearl_pinned_alloc(size_t);
void pearl_pinned_free(void*);
void pearl_finalize();
}
static void die(const char* s) { fprintf(stderr, "profile_probe: %s\n", s); exit(2); }
static void writeAll(FILE* f, const void* p, size_t n) {
    if (fwrite(p, 1, n, f) != n) die("write failed");
}
static uint32_t nextRandom(uint32_t& s) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s;
}
int main(int argc, char** argv) {
    if (argc < 2 || argc > 9) {
        fprintf(stderr, "usage: %s OUTPUT.bin [device=0] [nbands=83] [strips=6] [n_hi=64] [target=zero|easy] [path=ptr|copy|fast] [scans=2]\n", argv[0]);
        return 2;
    }
    int dev = argc > 2 ? atoi(argv[2]) : 0;
    int nbands = argc > 3 ? atoi(argv[3]) : 83;
    int strips = argc > 4 ? atoi(argv[4]) : 6;
    int nhi = argc > 5 ? atoi(argv[5]) : 64;
    bool easy = argc > 6 && strcmp(argv[6], "easy") == 0;
    const char* path = argc > 7 ? argv[7] : "ptr";
    int scans = argc > 8 ? atoi(argv[8]) : 2;
    bool fast = !strcmp(path, "fast"), copy = !strcmp(path, "copy");
    if (dev < 0 || nbands < 1 || nbands > 1024 || strips < 1 || strips > 256 ||
        nhi < 1 || nhi > 64 || scans < 1 || scans > 100 ||
        (argc > 6 && strcmp(argv[6], "zero") && strcmp(argv[6], "easy")) ||
        (!fast && !copy && strcmp(path, "ptr"))) die("invalid arguments");
    if (getenv("PRL_NOFOLD")) die("unset PRL_NOFOLD for an equivalence check");
    const size_t aBytes = 128ULL * PEARL_PROBE_K;
    const size_t bBytes = (size_t)nbands * 64 * PEARL_PROBE_K;
    const size_t transBytes = (size_t)nbands * 16 * 64 * 4;
    if (pearl_init(dev, nbands)) die("pearl_init failed");
    auto A = (int8_t*)pearl_pinned_alloc(aBytes * strips);
    auto B = (int8_t*)pearl_pinned_alloc(bBytes);
    if (!A || !B) die("pinned allocation failed");
    uint32_t rng = 0x5827ac31;
    for (size_t i = 0; i < aBytes * strips; ++i) A[i] = (int8_t)((int)(nextRandom(rng) % 5) - 2);
    for (size_t i = 0; i < bBytes; ++i) B[i] = (int8_t)((int)(nextRandom(rng) % 5) - 2);
    uint32_t key[8], target[8];
    for (int i = 0; i < 8; ++i) { key[i] = nextRandom(rng); target[i] = easy ? UINT32_MAX : 0; }
    pearl_set_b(B, nbands * 64);
    pearl_set_nhi(nhi);
    FILE* f = fopen(argv[1], "wb"); if (!f) die("cannot open output");
    uint32_t header[] = {0x50524f42, PEARL_PROBE_K, (uint32_t)nbands, (uint32_t)strips,
                        (uint32_t)nhi, easy ? 1U : 0U, fast ? 1U : 0U, (uint32_t)scans};
    writeAll(f, header, sizeof(header));
    std::vector<uint32_t> transcript(transBytes / 4);
    for (int scan = 0; scan < scans; ++scan) {
        if (scan) pearl_use_b(0);
        pearl_set_key_target(key, target);
        int submitted = 0;
        auto submit = [&](int i) {
            if (fast) pearl_strip_submit_fast(A + (size_t)i * aBytes);
            else pearl_strip_submit(A + (size_t)i * aBytes);
        };
        while (submitted < std::min(strips, PEARL_PIPE_DEPTH)) submit(submitted++);
        for (int i = 0; i < strips; ++i) {
            if (fast) pearl_strip_wait_done();
            else if (copy) { pearl_strip_wait(transcript.data()); writeAll(f, transcript.data(), transBytes); }
            else writeAll(f, pearl_strip_wait_ptr(), transBytes);
            int hitStrip = -1, hitTile = -1;
            int hit = pearl_get_hit_result(&hitStrip, &hitTile);
            if (hit && (hitStrip != i || hitTile < 0 || hitTile >= nbands * nhi)) die("invalid hit encoding");
            if (easy && !hit) die("all-ones target must produce a hit");
            int32_t hitRecord[] = {hit, hitStrip, hitTile};
            writeAll(f, hitRecord, sizeof(hitRecord));
            if (submitted < strips) submit(submitted++);
        }
    }
    if (fclose(f)) die("close failed");
    pearl_pinned_free(A); pearl_pinned_free(B); pearl_finalize();
    fprintf(stderr, "profile_probe: deterministic %s output written to %s\n", path, argv[1]);
}
