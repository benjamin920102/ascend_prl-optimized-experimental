/* Standalone source-level profiling ABI. No device SDK dependency. */
#ifndef PEARL_PROFILE_SCHEMA_H
#define PEARL_PROFILE_SCHEMA_H
#include <stdint.h>
#ifndef PEARL_PROFILE
#define PEARL_PROFILE 1
#endif
#define PEARL_PROF_MAGIC UINT64_C(0x504541524c505231)
#define PEARL_PROF_VERSION 1
/* name, label, source space, destination space, interpretation */
#define PEARL_PROF_STAGES(X) \
 X(TOTAL, "kernel_total", "none", "none", "inclusive_wall") \
 X(SETUP, "kernel_setup", "none", "none", "scalar_region") \
 X(FLAG_WAIT, "flag_spin_wait", "none", "none", "inclusive_wait") \
 X(FLAG_STORE, "flag_store_flush", "scalar", "GM", "scalar_region") \
 X(FLAG_POLLS, "flag_poll_count", "GM", "scalar", "counter_only") \
 X(BACKOFF, "backoff_loop_iterations", "none", "none", "counter_only") \
 X(FLAG_REFRESH, "flag_dcci_and_load", "none", "none", "nested_mode2_scalar_region") \
 X(BACKOFF_WAIT, "backoff_delay", "none", "none", "nested_mode2_scalar_region") \
 X(QUEUE_ALLOC, "queue_alloc", "none", "none", "queue_api") \
 X(COPY_IN, "partial_GM_to_UB", "GM", "UB", "issue_or_fenced") \
 X(QUEUE_ENQ, "queue_enque", "none", "none", "queue_api") \
 X(QUEUE_DEQ, "queue_deque", "none", "none", "queue_api") \
 X(QUEUE_FREE, "queue_free", "none", "none", "queue_api") \
 X(RELEASE_WAIT, "MTE2_S_release_wait", "none", "none", "completion_wait") \
 X(DUPLICATE, "UB_zero_acc_transcript", "none", "UB", "issue_or_fenced") \
 X(ADD, "UB_accumulate_Add", "UB", "UB", "issue_or_fenced") \
 X(XOR_OR, "UB_xor_Or", "UB", "UB", "issue_or_fenced") \
 X(XOR_AND, "UB_xor_And", "UB", "UB", "issue_or_fenced") \
 X(XOR_SUB, "UB_xor_Sub", "UB", "UB", "issue_or_fenced") \
 X(SHIFT_LEFT, "UB_rotate_ShiftLeft", "UB", "UB", "issue_or_fenced") \
 X(SHIFT_RIGHT, "UB_rotate_ShiftRight", "UB", "UB", "issue_or_fenced") \
 X(ROTATE_OR, "UB_rotate_Or", "UB", "UB", "issue_or_fenced") \
 X(BARRIER_V, "PipeBarrier_V", "none", "none", "issue_or_fenced") \
 X(BARRIER_ALL, "PipeBarrier_ALL", "none", "none", "issue_or_fenced") \
 X(EXPORT, "transcript_UB_to_GM", "UB", "GM", "issue_or_fenced") \
 X(PREFETCH_POLL, "lookahead_flag_refresh", "GM", "scalar", "scalar_region") \
 X(UB_READ, "transcript_UB_GetValue", "UB", "scalar", "scalar_load_region") \
 X(HASH, "scalar_BLAKE3", "scalar", "scalar", "scalar_region") \
 X(TARGET, "scalar_target_compare", "scalar", "scalar", "scalar_region") \
 X(HIT_STORE, "hit_store_flush", "scalar", "GM", "scalar_region") \
 X(KEY_READ, "key_target_GM_load", "GM", "scalar", "scalar_load_region") \
 X(MM_SETUP, "matmul_register_shape", "none", "none", "library_api") \
 X(MM_SETA, "matmul_SetTensorA", "none", "none", "library_api") \
 X(MM_SETB, "matmul_SetTensorB", "none", "none", "library_api") \
 X(MM_ITERATE, "matmul_IterateAll", "GM", "GM", "library_api_logical_bytes") \
 X(MM_END, "matmul_End", "none", "none", "library_api") \
 X(PROFILE_FENCE, "profiling_added_fence", "none", "none", "nested_completion_wait")
enum PearlProfStage {
#define PEARL_PROF_ENUM(n, label, src, dst, meaning) PP_##n,
    PEARL_PROF_STAGES(PEARL_PROF_ENUM)
#undef PEARL_PROF_ENUM
    PP_STAGE_COUNT
};
enum { PP_CUBE = 0, PP_VECTOR = 1 };
/* Fixed 64-byte records avoid inter-worker D-cache line sharing. */
struct PearlProfMetric {
    uint64_t calls, ticks, min_ticks, max_ticks, read_bytes, write_bytes;
};
struct PearlProfTrace {
    /* slot: low 32 bits = signed ring slot; high 32 bits = signed tile hi. */
    uint64_t start, ticks, stage, band, fold, read_bytes, write_bytes, slot;
};
enum {
    PP_H_MAGIC, PP_H_VERSION, PP_H_KIND, PP_H_WORKER, PP_H_STRIP, PP_H_MODE,
    PP_H_START, PP_H_END, PP_H_TRACE_COUNT, PP_H_TRACE_DROPPED, PP_H_TRACE_CAP,
    PP_H_NBANDS, PP_H_NFOLD, PP_H_BUFS, PP_H_VEC2X, PP_H_RESERVED,
    PP_HEADER_WORDS
};
#define PEARL_PROF_WORKER_BYTES \
    (((PP_HEADER_WORDS * 8 + PP_STAGE_COUNT * sizeof(PearlProfMetric) + 511) / 512) * 512)
static_assert(sizeof(PearlProfTrace) == 64, "trace ABI");
#endif
