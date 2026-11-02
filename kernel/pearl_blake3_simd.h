/* Experimental BLAKE3 across transcript columns. Included after the scalar
 * BLAKE3 constants. No native Xor or cross-core synchronization is required.
 * Each row is 64 uint32 lanes; input and state must not alias the scratch.
 */
#ifndef PEARL_BLAKE3_SIMD_H
#define PEARL_BLAKE3_SIMD_H
namespace pearl_simd {
using U32 = AscendC::LocalTensor<uint32_t>;
constexpr int LANES = 64;

__aicore__ inline void Add32(U32 dst, U32 a, U32 b) {
    // Device integer Add wraps at 32 bits; do not cast numerically to float.
    AscendC::Add(dst.ReinterpretCast<int32_t>(), a.ReinterpretCast<int32_t>(),
                 b.ReinterpretCast<int32_t>(), LANES);
    AscendC::PipeBarrier<PIPE_V>();
}
__aicore__ inline void Xor32(U32 dst, U32 a, U32 b, U32 t0, U32 t1) {
    AscendC::Or(t0.ReinterpretCast<uint16_t>(), a.ReinterpretCast<uint16_t>(),
                b.ReinterpretCast<uint16_t>(), LANES * 2);
    AscendC::And(t1.ReinterpretCast<uint16_t>(), a.ReinterpretCast<uint16_t>(),
                 b.ReinterpretCast<uint16_t>(), LANES * 2);
    AscendC::PipeBarrier<PIPE_V>();
    AscendC::Sub(dst.ReinterpretCast<int16_t>(), t0.ReinterpretCast<int16_t>(),
                 t1.ReinterpretCast<int16_t>(), LANES * 2);
    AscendC::PipeBarrier<PIPE_V>();
}
__aicore__ inline void Rotr32(U32 dst, uint32_t n, U32 t0, U32 t1) {
    AscendC::ShiftRight(t0, dst, n, LANES);
    AscendC::ShiftLeft(t1, dst, uint32_t(32) - n, LANES);
    AscendC::PipeBarrier<PIPE_V>();
    AscendC::Or(dst.ReinterpretCast<uint16_t>(), t0.ReinterpretCast<uint16_t>(),
                t1.ReinterpretCast<uint16_t>(), LANES * 2);
    AscendC::PipeBarrier<PIPE_V>();
}
__aicore__ inline void G(U32 state, int a, int b, int c, int d,
                         U32 x, U32 y, U32 t0, U32 t1) {
    U32 A = state[a * LANES], B = state[b * LANES];
    U32 C = state[c * LANES], D = state[d * LANES];
    Add32(A, A, B); Add32(A, A, x);
    Xor32(D, D, A, t0, t1); Rotr32(D, 16, t0, t1);
    Add32(C, C, D);
    Xor32(B, B, C, t0, t1); Rotr32(B, 12, t0, t1);
    Add32(A, A, B); Add32(A, A, y);
    Xor32(D, D, A, t0, t1); Rotr32(D, 8, t0, t1);
    Add32(C, C, D);
    Xor32(B, B, C, t0, t1); Rotr32(B, 7, t0, t1);
}
// One complete keyed 64-byte block per lane. Results occupy state[0..7].
// Compute all 64 lanes; callers consume only [0,n_hi). No transpose needed.
__aicore__ inline void Hash(U32 state, U32 input, const uint32_t* key,
                            U32 t0, U32 t1) {
    for (int i = 0; i < 8; ++i) AscendC::Duplicate(state[i * LANES], key[i], LANES);
    for (int i = 0; i < 4; ++i) AscendC::Duplicate(state[(8 + i) * LANES], BLAKE3_IV[i], LANES);
    AscendC::Duplicate(state[12 * LANES], uint32_t(0), LANES);
    AscendC::Duplicate(state[13 * LANES], uint32_t(0), LANES);
    AscendC::Duplicate(state[14 * LANES], uint32_t(64), LANES);
    AscendC::Duplicate(state[15 * LANES], uint32_t(BLAKE3_FLAG_KEYED |
        BLAKE3_FLAG_CHUNK_START | BLAKE3_FLAG_CHUNK_END | BLAKE3_FLAG_ROOT), LANES);
    AscendC::PipeBarrier<PIPE_V>();
    for (int r = 0; r < 7; ++r) {
        const int32_t* s = BLAKE3_SIGMA[r];
        G(state, 0, 4, 8, 12, input[s[0]*LANES], input[s[1]*LANES], t0, t1);
        G(state, 1, 5, 9, 13, input[s[2]*LANES], input[s[3]*LANES], t0, t1);
        G(state, 2, 6, 10, 14, input[s[4]*LANES], input[s[5]*LANES], t0, t1);
        G(state, 3, 7, 11, 15, input[s[6]*LANES], input[s[7]*LANES], t0, t1);
        G(state, 0, 5, 10, 15, input[s[8]*LANES], input[s[9]*LANES], t0, t1);
        G(state, 1, 6, 11, 12, input[s[10]*LANES], input[s[11]*LANES], t0, t1);
        G(state, 2, 7, 8, 13, input[s[12]*LANES], input[s[13]*LANES], t0, t1);
        G(state, 3, 4, 9, 14, input[s[14]*LANES], input[s[15]*LANES], t0, t1);
    }
    for (int i = 0; i < 8; ++i)
        Xor32(state[i * LANES], state[i * LANES], state[(i + 8) * LANES], t0, t1);
}
}  // namespace pearl_simd
#endif
