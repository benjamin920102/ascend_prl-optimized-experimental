/* Test the actual scan_full implementation with a mocked NPU submission API. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/scan_async.c"

static int submitted, finished, hit_after, max_inflight;
static int set_keys, set_hi;
static const int8_t *scan_base;
void pearl_strip_submit_fast(const int8_t *a) {
    assert(a == scan_base + (size_t)submitted * R * K);
    ++submitted;
    assert(submitted - finished <= 2);
    if (submitted - finished > max_inflight) max_inflight = submitted - finished;
}
void pearl_strip_wait_done(void) {
    assert(submitted > finished);
    ++finished;
}
void pearl_set_key_target(const uint32_t *k, const uint32_t *t) {
    assert(k && t);
    ++set_keys;
}
void pearl_set_nhi(int n) { assert(n > 0); ++set_hi; }
int pearl_get_hit_result(int *hs, int *ht) {
    if (finished == hit_after) { *hs = finished - 1; *ht = 7; return 1; }
    return 0;
}

static void run_case(int nsuper, int on_hit) {
    submitted = finished = max_inflight = 0;
    set_keys = set_hi = 0;
    hit_after = on_hit;
    uint8_t key[32] = {0};
    uint32_t target[8] = {0};
    static int8_t dummy[5 * R * K]; // valid address range for five strips.
    scan_base = dummy;
    int hs = -1, ht = -1;
    int found = scan_full(dummy, nsuper, key, target,
                          80, 2, 1, NULL, &hs, &ht);
    assert(found == (on_hit >= 1 && on_hit <= nsuper));
    assert(submitted == finished); // critical: no in-flight job at return
    if (found) {
        assert(hs == on_hit - 1 && ht == 7);
        assert(submitted == (on_hit < nsuper ? on_hit + 1 : on_hit));
    } else {
        assert(submitted == nsuper);
    }
    assert(set_keys == 1 && set_hi == 1);
    assert(max_inflight <= 2);
}

int main(void) {
    {
        run_case(5, 1); // early hit with next strip already submitted
        run_case(5, 3); // hit after several strips
        run_case(5, 5); // last-strip hit
        run_case(5, 0); // no hit
        run_case(1, 1); // no outstanding next strip
        run_case(1, 0);
        run_case(0, 0); // empty scan submits no work
    }
    puts("PASS: Single-strip offsets and in-flight drain before reuse");
    return 0;
}
