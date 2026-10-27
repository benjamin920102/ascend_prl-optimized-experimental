/* Included by the host portion of pearl_hlc2_cube.asc after CheckAcl. */
#ifndef PEARL_PROFILE_HOST_H
#define PEARL_PROFILE_HOST_H
#include "pearl_profile_schema.h"
#include <cstdio>
#include <cerrno>
#include <chrono>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

static uint64_t ProfHostNow() {
#if PEARL_PROFILE
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
#else
    return 0;
#endif
}

#if PEARL_PROFILE
static const char* const kProfNames[] = {
#define PP_NAME(n, label, src, dst, meaning) label,
    PEARL_PROF_STAGES(PP_NAME)
#undef PP_NAME
};
static const char* const kProfSrc[] = {
#define PP_SRC(n, label, src, dst, meaning) src,
    PEARL_PROF_STAGES(PP_SRC)
#undef PP_SRC
};
static const char* const kProfDst[] = {
#define PP_DST(n, label, src, dst, meaning) dst,
    PEARL_PROF_STAGES(PP_DST)
#undef PP_DST
};
static const char* const kProfMeaning[] = {
#define PP_MEANING(n, label, src, dst, meaning) meaning,
    PEARL_PROF_STAGES(PP_MEANING)
#undef PP_MEANING
};
static int g_profMode = 0, g_profCap = 0;
static uint64_t g_profLimit = 4, g_profEvery = 1, g_profOrdinal = 0, g_profSelected = 0, g_profScan = 0;
static std::string g_profBase;
static FILE* g_profCsv = nullptr;
static FILE* g_profHostCsv = nullptr;
static constexpr int PROF_WORKERS = GRID * (1 + VEC2X);
static constexpr size_t PROF_STATS_BYTES = PROF_WORKERS * PEARL_PROF_WORKER_BYTES;
static size_t g_profTraceBytes = 0;
static uint8_t* g_profD[PEARL_PIPE_DEPTH] = {};
static uint8_t* g_profH[PEARL_PIPE_DEPTH] = {};
static uint8_t* g_traceD[PEARL_PIPE_DEPTH] = {};
static uint8_t* g_traceH[PEARL_PIPE_DEPTH] = {};
static aclrtEvent g_profCubeDone[PEARL_PIPE_DEPTH] = {};
static bool g_profActive[PEARL_PIPE_DEPTH] = {};
static uint64_t g_profSample[PEARL_PIPE_DEPTH] = {}, g_profSlotScan[PEARL_PIPE_DEPTH] = {};
static uint64_t g_profSubmitNs[PEARL_PIPE_DEPTH] = {}, g_profEnqueueNs[PEARL_PIPE_DEPTH] = {};
static uint64_t ProfEnv(const char* name, uint64_t fallback, uint64_t maxValue) {
    const char* s = getenv(name);
    if (!s) return fallback;
    errno = 0; char* end = nullptr;
    unsigned long long v = strtoull(s, &end, 10);
    if (!*s || *s == '-' || errno || *end || v > maxValue) {
        std::cerr << "[pearl-prof] invalid " << name << "=" << s << std::endl; exit(2);
    }
    return (uint64_t)v;
}
static void ProfMkdir(const std::string& path) {
    for (size_t i = 1; i <= path.size(); ++i) {
        if (i != path.size() && path[i] != '/') continue;
        std::string p = path.substr(0, i);
        if (mkdir(p.c_str(), 0700) && errno != EEXIST) {
            std::cerr << "[pearl-prof] cannot create " << p << std::endl; exit(2);
        }
    }
}
static FILE* ProfOpen(const std::string& path) {
    FILE* f = fopen(path.c_str(), "w");
    if (!f) { std::cerr << "[pearl-prof] cannot write " << path << std::endl; exit(2); }
    return f;
}
static void ProfCheckFile(FILE* f) {
    if (fflush(f) || ferror(f)) { std::cerr << "[pearl-prof] output write failed" << std::endl; exit(2); }
}
static void ProfInit() {
    // The release built by the existing Actions workflow needs no new arguments.
    g_profMode = (int)ProfEnv("PRL_PROF", 2, 2);
    if (!g_profMode) return;
    g_profCap = (int)ProfEnv("PRL_PROF_TRACE", 4096, 16384);
    g_profLimit = ProfEnv("PRL_PROF_STRIPS", 4, UINT64_MAX);
    g_profEvery = ProfEnv("PRL_PROF_EVERY", 1, UINT64_MAX);
    if (!g_profEvery) { std::cerr << "[pearl-prof] PRL_PROF_EVERY must be positive" << std::endl; exit(2); }
    const char* env = getenv("PRL_PROF_OUT");
    std::string dir = env && *env ? env : "profile";
    ProfMkdir(dir);
    g_profBase = dir + "/dev" + std::to_string(g_dev) + "_pid" + std::to_string(getpid());
    g_profCsv = ProfOpen(g_profBase + "_stats.csv");
    fprintf(g_profCsv, "sample,scan,strip,kind,worker,stage_id,stage,semantics,mode,calls,ticks,total_us,min_us,max_us,mean_us,source,destination,read_bytes,write_bytes,trace_count,trace_dropped\n");
    g_profHostCsv = ProfOpen(g_profBase + "_host.csv");
    fprintf(g_profHostCsv, "sample,scan,strip,submit_enqueue_us,host_wait_event_us,device_stats_D2H_bytes,device_trace_D2H_bytes,collect_output_us\n");
    g_profTraceBytes = (size_t)PROF_WORKERS * g_profCap * sizeof(PearlProfTrace);
    for (int s = 0; s < PEARL_PIPE_DEPTH; ++s) {
        CheckAcl(aclrtMalloc((void**)&g_profD[s], PROF_STATS_BYTES, ACL_MEM_MALLOC_HUGE_FIRST), "allocate profile stats");
        CheckAcl(aclrtMallocHost((void**)&g_profH[s], PROF_STATS_BYTES), "allocate pinned profile stats");
        if (g_profTraceBytes) {
            CheckAcl(aclrtMalloc((void**)&g_traceD[s], g_profTraceBytes, ACL_MEM_MALLOC_HUGE_FIRST), "allocate profile trace");
            CheckAcl(aclrtMallocHost((void**)&g_traceH[s], g_profTraceBytes), "allocate pinned profile trace");
        }
        CheckAcl(aclrtCreateEventExWithFlag(&g_profCubeDone[s], ACL_EVENT_SYNC), "create profiling cube event");
    }
    FILE* meta = ProfOpen(g_profBase + "_metadata.json");
    fprintf(meta, "{\"schema_version\":%d,\"clock_MHz\":50,\"mode\":%d,\"grid\":%d,\"vec2x\":%d,\"bufs\":%d,\"ninner\":%d,\"nfold\":%d,\"nbands\":%d,\"pipe_depth\":%d,\"trace_capacity_per_worker\":%d,\"sample_limit\":%llu,\"sample_every\":%llu,\"profile_device_bytes\":%llu,\"profile_pinned_host_bytes\":%llu,\"declared_vector_UB_bytes\":%d,\"note\":\"Source-level regions; logical operand bytes; Cube library internals opaque; profiling perturbs scheduling and may add compiler spills.\"}\n",
        PEARL_PROF_VERSION, g_profMode, GRID, VEC2X, BUFS, NINNER, NFOLD, g_nbands, PEARL_PIPE_DEPTH,
        g_profCap, (unsigned long long)g_profLimit, (unsigned long long)g_profEvery,
        (unsigned long long)((PROF_STATS_BYTES + g_profTraceBytes) * PEARL_PIPE_DEPTH),
        (unsigned long long)((PROF_STATS_BYTES + g_profTraceBytes) * PEARL_PIPE_DEPTH),
        (2 * PART_ELEMS + PART_ELEMS + PART_ELEMS / 2 + WORDS * BN + 2 * (BN / 2) * MM_N) * 4);
    ProfCheckFile(meta); fclose(meta);
    std::cerr << "[pearl-prof] AUTO profiling enabled; no additional launch arguments required" << std::endl;
    std::cerr << "[pearl-prof] standalone mode=" << g_profMode << " output=" << g_profBase
              << " samples=" << g_profLimit << " (0=unlimited) trace/worker=" << g_profCap
              << " device bytes=" << (PROF_STATS_BYTES + g_profTraceBytes) * PEARL_PIPE_DEPTH << std::endl;
}
static bool ProfSubmit(int s) {
    if (!g_profMode) return false;
    uint64_t ordinal = g_profOrdinal++;
    bool take = (!g_profLimit || g_profSelected < g_profLimit) && ordinal % g_profEvery == 0;
    g_profActive[s] = take;
    if (take) {
        g_profSample[s] = g_profSelected++; g_profSlotScan[s] = g_profScan;
        g_profSubmitNs[s] = ProfHostNow();
    }
    return take;
}
static void ProfEnqueued(int s) {
    if (g_profActive[s]) g_profEnqueueNs[s] = ProfHostNow() - g_profSubmitNs[s];
}
static void ProfCollect(int s, uint64_t waitStart) {
    if (!g_profActive[s]) return;
    uint64_t collectStart = ProfHostNow();
    double waitUs = waitStart ? (collectStart - waitStart) / 1000.0 : 0.0;
    uint64_t origin = UINT64_MAX;
    uint64_t traceDropped = 0;
    for (int w = 0; w < PROF_WORKERS; ++w) {
        auto h = (const uint64_t*)(g_profH[s] + w * PEARL_PROF_WORKER_BYTES);
        if (h[PP_H_MAGIC] != PEARL_PROF_MAGIC || h[PP_H_VERSION] != PEARL_PROF_VERSION ||
            h[PP_H_WORKER] != (uint64_t)w || h[PP_H_KIND] != (uint64_t)(w < GRID ? PP_CUBE : PP_VECTOR) ||
            h[PP_H_TRACE_CAP] != (uint64_t)g_profCap || h[PP_H_TRACE_COUNT] > (uint64_t)g_profCap ||
            h[PP_H_MODE] != (uint64_t)g_profMode) {
            std::cerr << "[pearl-prof] invalid/incomplete profile worker=" << w << std::endl; exit(2);
        }
        if (h[PP_H_START] < origin) origin = h[PP_H_START];
        traceDropped += h[PP_H_TRACE_DROPPED];
    }
    FILE* trace = nullptr; bool comma = false;
    if (g_profCap) {
        trace = ProfOpen(g_profBase + "_sample" + std::to_string(g_profSample[s]) + "_trace.json");
        fprintf(trace, "{\"displayTimeUnit\":\"us\",\"traceEvents\":[");
    }
    PearlProfMetric agg[2][PP_STAGE_COUNT] = {};
    uint64_t longest[2] = {};
    for (int w = 0; w < PROF_WORKERS; ++w) {
        auto h = (const uint64_t*)(g_profH[s] + w * PEARL_PROF_WORKER_BYTES);
        auto m = (const PearlProfMetric*)(h + PP_HEADER_WORDS);
        int kind = (int)h[PP_H_KIND];
        const char* name = kind == PP_CUBE ? "cube" : "vector";
        if (m[PP_TOTAL].ticks > longest[kind]) longest[kind] = m[PP_TOTAL].ticks;
        for (int i = 0; i < PP_STAGE_COUNT; ++i) {
            const auto& a = m[i]; if (!a.calls) continue;
            fprintf(g_profCsv, "%llu,%llu,%llu,%s,%d,%d,%s,%s,%d,%llu,%llu,%.3f,%.3f,%.3f,%.6f,%s,%s,%llu,%llu,%llu,%llu\n",
                (unsigned long long)g_profSample[s], (unsigned long long)g_profSlotScan[s], (unsigned long long)h[PP_H_STRIP],
                name, w, i, kProfNames[i], kProfMeaning[i], g_profMode,
                (unsigned long long)a.calls, (unsigned long long)a.ticks, a.ticks / 50.0,
                a.min_ticks / 50.0, a.max_ticks / 50.0, a.ticks / (50.0 * a.calls), kProfSrc[i], kProfDst[i],
                (unsigned long long)a.read_bytes, (unsigned long long)a.write_bytes,
                (unsigned long long)h[PP_H_TRACE_COUNT], (unsigned long long)h[PP_H_TRACE_DROPPED]);
            auto& b = agg[kind][i]; b.calls += a.calls; b.ticks += a.ticks;
            b.read_bytes += a.read_bytes; b.write_bytes += a.write_bytes;
            if (a.max_ticks > b.max_ticks) b.max_ticks = a.max_ticks;
        }
        if (trace) {
            fprintf(trace, "%s{\"name\":\"%s_block_%d\",\"cat\":\"kernel\",\"ph\":\"X\",\"pid\":%d,\"tid\":%d,\"ts\":%.3f,\"dur\":%.3f,\"args\":{\"strip\":%llu,\"trace_dropped\":%llu}}",
                comma ? "," : "", name, w, g_dev, w, (h[PP_H_START] - origin) / 50.0,
                m[PP_TOTAL].ticks / 50.0, (unsigned long long)h[PP_H_STRIP], (unsigned long long)h[PP_H_TRACE_DROPPED]);
            comma = true;
            auto events = (const PearlProfTrace*)(g_traceH[s] + (size_t)w * g_profCap * sizeof(PearlProfTrace));
            for (uint64_t j = 0; j < h[PP_H_TRACE_COUNT]; ++j) {
                auto& e = events[j];
                if (e.stage >= PP_STAGE_COUNT || e.start < origin) {
                    std::cerr << "[pearl-prof] corrupt trace" << std::endl; exit(2);
                }
                fprintf(trace, ",{\"name\":\"%s\",\"cat\":\"source_region\",\"ph\":\"X\",\"pid\":%d,\"tid\":%d,\"ts\":%.3f,\"dur\":%.3f,\"args\":{\"band\":%lld,\"fold\":%lld,\"slot\":%d,\"tile_hi\":%d,\"source\":\"%s\",\"destination\":\"%s\",\"logical_read_bytes\":%llu,\"logical_write_bytes\":%llu}}",
                    kProfNames[e.stage], g_dev, w, (e.start - origin) / 50.0, e.ticks / 50.0,
                    (long long)e.band, (long long)e.fold, (int)(int32_t)e.slot,
                    (int)(int32_t)(e.slot >> 32), kProfSrc[e.stage], kProfDst[e.stage],
                    (unsigned long long)e.read_bytes, (unsigned long long)e.write_bytes);
            }
        }
    }
    if (trace) { fprintf(trace, "]}\n"); ProfCheckFile(trace); fclose(trace); }
    for (int k = 0; k < 2; ++k) {
        fprintf(stderr, "[pearl-prof] sample=%llu scan=%llu %s max_block_us=%.3f (sum_us below sums workers)\n",
            (unsigned long long)g_profSample[s], (unsigned long long)g_profSlotScan[s], k ? "vector" : "cube", longest[k] / 50.0);
        for (int i = 0; i < PP_STAGE_COUNT; ++i) {
            auto& a = agg[k][i]; if (!a.calls) continue;
            fprintf(stderr, "  %-29s n=%llu sum_us=%.3f max_call_us=%.3f read_B=%llu write_B=%llu\n",
                kProfNames[i], (unsigned long long)a.calls, a.ticks / 50.0, a.max_ticks / 50.0,
                (unsigned long long)a.read_bytes, (unsigned long long)a.write_bytes);
        }
    }
    if (traceDropped) fprintf(stderr, "[pearl-prof] trace dropped=%llu; summary still counts all calls\n", (unsigned long long)traceDropped);
    uint64_t* first = (uint64_t*)g_profH[s];
    ProfCheckFile(g_profCsv);
    fprintf(g_profHostCsv, "%llu,%llu,%llu,%.3f,%.3f,%llu,%llu,%.3f\n",
        (unsigned long long)g_profSample[s], (unsigned long long)g_profSlotScan[s], (unsigned long long)first[PP_H_STRIP],
        g_profEnqueueNs[s] / 1000.0, waitUs, (unsigned long long)PROF_STATS_BYTES,
        (unsigned long long)g_profTraceBytes, (ProfHostNow() - collectStart) / 1000.0);
    ProfCheckFile(g_profHostCsv);
    g_profActive[s] = false;
}
static void ProfShutdown() {
    if (!g_profMode) return;
    // Called after both streams are drained, so unconsumed submitted slots are safe.
    for (int s = 0; s < PEARL_PIPE_DEPTH; ++s) {
        ProfCollect(s, 0);
        if (g_profD[s]) CheckAcl(aclrtFree(g_profD[s]), "free profile stats");
        if (g_profH[s]) CheckAcl(aclrtFreeHost(g_profH[s]), "free pinned profile stats");
        if (g_traceD[s]) CheckAcl(aclrtFree(g_traceD[s]), "free profile trace");
        if (g_traceH[s]) CheckAcl(aclrtFreeHost(g_traceH[s]), "free pinned profile trace");
        if (g_profCubeDone[s]) CheckAcl(aclrtDestroyEvent(g_profCubeDone[s]), "destroy profile event");
    }
    fclose(g_profCsv); fclose(g_profHostCsv);
}
#else
static void ProfInit() {
    const char* s = getenv("PRL_PROF");
    if (s && strcmp(s, "0")) std::cerr << "[pearl-prof] instrumentation was compiled out; rebuild using the default Actions workflow" << std::endl;
}
static void ProfCollect(int, uint64_t) {}
static void ProfShutdown() {}
#endif
#endif
