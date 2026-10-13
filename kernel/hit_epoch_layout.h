#pragma once
/* Shared epoch-hit layout for AscendC device and C++ host builds.
 * Use a separate 512-byte cache region per consumer block so dcci writebacks
 * do not accidentally write back another core's hit record.
 * Each record is encoded_tile, epoch_lo, epoch_hi; remaining words reserved.
 */
#define PEARL_HIT_STRIDE_WORDS 128
#define PEARL_HIT_TILE_WORD 0
#define PEARL_HIT_EPOCH_LO_WORD 1
#define PEARL_HIT_EPOCH_HI_WORD 2
