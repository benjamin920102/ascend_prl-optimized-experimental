/* Standalone host regression for the production hit decoder. No CANN needed. */
#include "../kernel/hit_epoch_decode.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

constexpr size_t WORKERS = 40;
constexpr size_t STRIDE = PEARL_HIT_STRIDE_WORDS;

static void emit(std::vector<uint32_t>& mem, size_t worker, uint32_t tile, uint64_t tag) {
    assert(worker < WORKERS);
    uint32_t* r = mem.data() + STRIDE * worker;
    r[PEARL_HIT_TILE_WORD] = tile;
    r[PEARL_HIT_EPOCH_LO_WORD] = static_cast<uint32_t>(tag);
    r[PEARL_HIT_EPOCH_HI_WORD] = static_cast<uint32_t>(tag >> 32);
}

static void check_batch(int mbatch) {
    constexpr int BANDS = 80, NHI = 2;
    constexpr int TILES = BANDS * NHI;
    constexpr uint64_t EPOCH = 0x12345678fffffff1ULL;
    const int first = 5 * mbatch, last = first + mbatch - 1;
    std::vector<uint32_t> mem(WORKERS * STRIDE);
    int strip = -1, tile = -1;
    auto check = [&](uint64_t e, int s) {
        return pearl_decode_hit_epoch(mem.data(), WORKERS, e, s, mbatch,
                                      BANDS, NHI, &strip, &tile);
    };
    assert(!check(EPOCH, 5)); // zeroed records cannot be hits
    emit(mem, 3, first * TILES + 4, EPOCH - 1);
    assert(!check(EPOCH, 5)); // stale epoch cannot become a hit
    emit(mem, 17, first * TILES + 1, EPOCH);
    assert(check(EPOCH, 5));
    assert(strip == first && tile == 1);
    emit(mem, 35, last * TILES + 2, EPOCH);
    assert(check(EPOCH, 5)); // multiple independently owned hit records
    assert(strip == first && tile == 1); // choose smallest valid encoded index
    emit(mem, 17, (last + 1) * TILES + 1, EPOCH);
    assert(check(EPOCH, 5));
    assert(strip == last && tile == 2); // reject records for another super-strip
    assert(!check(EPOCH, 4));
    assert(!check(EPOCH + 1, 5)); // no-hit after reusing same memory
    emit(mem, 0, first * TILES + 90, EPOCH + 1);
    assert(check(EPOCH + 1, 5) && strip == first && tile == 90);
    // Matching low 32 bits but different high 32 bits must not pass.
    assert(!check(EPOCH + (1ULL << 32), 5));
    assert(!pearl_decode_hit_epoch(nullptr, WORKERS, EPOCH, 5, mbatch,
                                   BANDS, NHI, &strip, &tile));
}

int main() {
    for (int mbatch = 1; mbatch <= 8; mbatch *= 2) check_batch(mbatch);
    puts("PASS: MBATCH=1/2/4/8 epoch decoder, stale hit, reuse, index bounds, high bits");
}
