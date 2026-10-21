/* Submit single strips to the NPU; the vector kernel hashes and checks tile hits. */
#include "prl_timing.h"
#include <stdint.h>
#include <string.h>
#include "pearl_pipeline.h"

#define R 128
#ifndef K
#define K 4096
#endif

extern void pearl_strip_submit_fast(const int8_t *a);
extern void pearl_strip_wait_done(void);
extern void pearl_set_key_target(const uint32_t *key, const uint32_t *target);
extern void pearl_set_nhi(int n_hi);
extern int pearl_get_hit_result(int *hit_strip, int *hit_tile);

int scan_full(const int8_t *a_noised, int num_strips,
              const uint8_t *key, const uint32_t *target,
              int nbands, int n_hi, int pow_threads,
              uint32_t *scratch, int *hit_strip, int *hit_tile) {
    (void)scratch;
    (void)nbands;
    (void)pow_threads;

    if (num_strips <= 0) return 0;

    uint32_t key_u32[8];
    memcpy(key_u32, key, 32);
    double setup_start = prl_now_ms();
    pearl_set_key_target(key_u32, target);
    pearl_set_nhi(n_hi);
    prl_elapsed("scan.key_target_submit", setup_start);

    // Prime only as many strips as the host API has independent buffer slots.
    int submitted = 0;
    while (submitted < num_strips && submitted < PEARL_PIPE_DEPTH) {
        pearl_strip_submit_fast(a_noised + (size_t)submitted * R * K);
        ++submitted;
    }
    for (int completed = 0; completed < num_strips; ++completed) {
        double wait_start = prl_now_ms();
        pearl_strip_wait_done();
        prl_elapsed("scan.wait_done_host", wait_start);

        // Consume the completed slot BEFORE a refill can overwrite its pinned result.
        int h_strip, h_tile;
        double reduce_start = prl_now_ms();
        int hit = pearl_get_hit_result(&h_strip, &h_tile);
        prl_elapsed("scan.reduce_hits_cpu", reduce_start);
        if (hit) {
            *hit_strip = h_strip;
            *hit_tile = h_tile;
            // Drain every queued strip before the next scan changes keys/B/flags.
            double drain_start = prl_now_ms();
            for (int pending = completed + 1; pending < submitted; ++pending)
                pearl_strip_wait_done();
            prl_record("scan.hit_drain", prl_now_ms()-drain_start, submitted-completed-1);
            prl_record("scan.completed_strips", 0, submitted);
            return 1;
        }
        if (submitted < num_strips) {
            pearl_strip_submit_fast(a_noised + (size_t)submitted * R * K);
            ++submitted;
        }
    }

    prl_record("scan.completed_strips", 0, submitted);
    return 0;
}
