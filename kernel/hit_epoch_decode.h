#pragma once
/* Pure host-side result decoder: no ACL/CANN dependency. */
#include <stdint.h>
#include <stddef.h>
#include "hit_epoch_layout.h"

static inline int pearl_decode_hit_epoch(const uint32_t* records, size_t worker_count,
                                         uint64_t epoch, int strip, int mbatch,
                                         int nbands, int n_hi,
                                         int* hit_strip, int* hit_tile) {
    if (!records || !epoch || !hit_strip || !hit_tile || strip < 0 ||
        mbatch < 1 || nbands < 1 || n_hi < 1) return 0;
    const uint64_t tiles = (uint64_t)(unsigned)nbands * (unsigned)n_hi;
    const uint64_t lo = (uint64_t)(unsigned)strip * (unsigned)mbatch;
    const uint64_t hi = lo + (unsigned)mbatch;
    uint64_t best = UINT64_MAX;
    for (size_t i = 0; i < worker_count; i++) {
        const uint32_t* r = records + i * PEARL_HIT_STRIDE_WORDS;
        const uint64_t tag = ((uint64_t)r[PEARL_HIT_EPOCH_HI_WORD] << 32) |
                             r[PEARL_HIT_EPOCH_LO_WORD];
        if (tag != epoch) continue;
        const uint64_t encoded = r[PEARL_HIT_TILE_WORD];
        // Reject bad/stale records even if tags happened to match.
        const uint64_t batch_index = encoded / tiles;
        if (batch_index >= lo && batch_index < hi && encoded < best) best = encoded;
    }
    if (best == UINT64_MAX) return 0;
    *hit_strip = (int)(best / tiles);
    *hit_tile = (int)(best % tiles);
    return 1;
}
