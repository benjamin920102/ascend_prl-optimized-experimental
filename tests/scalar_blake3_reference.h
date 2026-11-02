// CPU-test reference only; not included in the device kernel.
__aicore__ inline uint32_t rotr32(uint32_t x, int32_t n) {
    return (x >> n) | (x << (32 - n));
}

__aicore__ inline void blake3_mixing_V(uint32_t* v, int32_t a, int32_t b, int32_t c, int32_t d, uint32_t x, uint32_t y) {
    v[a] = v[a] + v[b] + x;
    v[d] = rotr32(v[d] ^ v[a], 16);
    v[c] = v[c] + v[d];
    v[b] = rotr32(v[b] ^ v[c], 12);
    v[a] = v[a] + v[b] + y;
    v[d] = rotr32(v[d] ^ v[a], 8);
    v[c] = v[c] + v[d];
    v[b] = rotr32(v[b] ^ v[c], 7);
}

__aicore__ inline void blake3_compress(uint32_t* cv, const uint32_t* block, uint64_t counter, uint32_t flags) {
    // BLAKE3 state layout: chaining value, first four IV words, counter,
    // block length, and flags. These fields replace (not XOR into) the IV.
    uint32_t state[16];
    for (int32_t i = 0; i < 8; i++) state[i] = cv[i];
    state[8]  = BLAKE3_IV[0];
    state[9]  = BLAKE3_IV[1];
    state[10] = BLAKE3_IV[2];
    state[11] = BLAKE3_IV[3];
    state[12] = (uint32_t)(counter & 0xFFFFFFFF);
    state[13] = (uint32_t)((counter >> 32) & 0xFFFFFFFF);
    state[14] = BLAKE3_BLOCK_LEN;
    state[15] = flags;

    #pragma unroll
    for (int32_t round = 0; round < 7; round++) {
        int32_t s0 = BLAKE3_SIGMA[round][0];
        int32_t s1 = BLAKE3_SIGMA[round][1];
        int32_t s2 = BLAKE3_SIGMA[round][2];
        int32_t s3 = BLAKE3_SIGMA[round][3];
        int32_t s4 = BLAKE3_SIGMA[round][4];
        int32_t s5 = BLAKE3_SIGMA[round][5];
        int32_t s6 = BLAKE3_SIGMA[round][6];
        int32_t s7 = BLAKE3_SIGMA[round][7];
        int32_t s8 = BLAKE3_SIGMA[round][8];
        int32_t s9 = BLAKE3_SIGMA[round][9];
        int32_t s10 = BLAKE3_SIGMA[round][10];
        int32_t s11 = BLAKE3_SIGMA[round][11];
        int32_t s12 = BLAKE3_SIGMA[round][12];
        int32_t s13 = BLAKE3_SIGMA[round][13];
        int32_t s14 = BLAKE3_SIGMA[round][14];
        int32_t s15 = BLAKE3_SIGMA[round][15];

        blake3_mixing_V(state, 0, 4, 8, 12, block[s0], block[s1]);
        blake3_mixing_V(state, 1, 5, 9, 13, block[s2], block[s3]);
        blake3_mixing_V(state, 2, 6, 10, 14, block[s4], block[s5]);
        blake3_mixing_V(state, 3, 7, 11, 15, block[s6], block[s7]);

        blake3_mixing_V(state, 0, 5, 10, 15, block[s8], block[s9]);
        blake3_mixing_V(state, 1, 6, 11, 12, block[s10], block[s11]);
        blake3_mixing_V(state, 2, 7, 8, 13, block[s12], block[s13]);
        blake3_mixing_V(state, 3, 4, 9, 14, block[s14], block[s15]);
    }

    for (int32_t i = 0; i < 8; i++) {
        cv[i] = state[i] ^ state[i + 8];
    }
}

__aicore__ inline void blake3_hash_keyed(uint32_t* out, const uint32_t* input,
                                         const uint32_t* key, int32_t input_len) {
    // All callers provide exactly one complete 64-byte transcript block.
    (void)input_len;
    #pragma unroll
    for (int32_t i = 0; i < 8; ++i) out[i] = key[i];
    blake3_compress(out, input, 0, BLAKE3_FLAG_KEYED | BLAKE3_FLAG_CHUNK_START |
                                   BLAKE3_FLAG_CHUNK_END | BLAKE3_FLAG_ROOT);
}

