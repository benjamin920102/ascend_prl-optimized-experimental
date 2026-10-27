#ifndef PEARL_PROFILE_DEVICE_H
#define PEARL_PROFILE_DEVICE_H
#include "../src/pearl_profile_schema.h"

/* mode=1: no additional device fence. mode=2: serialize only exposed Vector /
 * MTE operations with explicit PIPE_* -> PIPE_S events. Cube remains an opaque
 * library call. All byte counts describe logical operands, not bus traffic. */
class PearlProfiler {
public:
#if PEARL_PROFILE
    __aicore__ inline PearlProfiler(__gm__ uint8_t* stats, __gm__ uint8_t* trace,
        int worker, int kind, int strip, int mode, int cap)
        : stats_(stats), trace_(trace), worker_(worker), kind_(kind), strip_(strip),
          mode_(stats ? mode : 0), cap_(cap) {
        if (!mode_) return;
        start_ = AscendC::GetSystemCycle();
        for (int s = 0; s < PP_STAGE_COUNT; ++s) {
            metrics_[s] = {0, 0, 0, 0, 0, 0};
        }
    }
    __aicore__ inline void InitVectorFences(AscendC::TPipe& pipe) {
        if (mode_ != 2) return;
        evV_ = static_cast<int32_t>(pipe.FetchEventID(AscendC::HardEvent::V_S));
        evIn_ = static_cast<int32_t>(pipe.FetchEventID(AscendC::HardEvent::MTE2_S));
        evOut_ = static_cast<int32_t>(pipe.FetchEventID(AscendC::HardEvent::MTE3_S));
    }
    __aicore__ inline uint64_t Begin() const {
        return mode_ ? (uint64_t)AscendC::GetSystemCycle() : 0;
    }
    __aicore__ inline void Context(int band, int fold, int slot) {
        band_ = band; fold_ = fold; slot_ = slot; tile_ = -1;
    }
    __aicore__ inline void Tile(int hi) { tile_ = hi; }
    __aicore__ inline uint64_t BeginPoll() { return mode_ == 2 ? Begin() : 0; }
    __aicore__ inline void EndPoll(int stage, uint64_t t) { if (mode_ == 2) End(stage, t); }
    __aicore__ inline void Count(int stage, uint64_t n, uint64_t rd = 0, uint64_t wr = 0) {
        if (!mode_) return;
        metrics_[stage].calls += n;
        metrics_[stage].read_bytes += rd;
        metrics_[stage].write_bytes += wr;
    }
    __aicore__ inline void End(int stage, uint64_t t, uint64_t rd = 0, uint64_t wr = 0) {
        if (!mode_) return;
        uint64_t end = AscendC::GetSystemCycle();
        uint64_t dt = end - t;
        auto& m = metrics_[stage];
        if (m.calls == 0 || dt < m.min_ticks) m.min_ticks = dt;
        ++m.calls; m.ticks += dt;
        if (dt > m.max_ticks) m.max_ticks = dt;
        m.read_bytes += rd; m.write_bytes += wr;
        if (trace_ && stage != PP_TOTAL) {
            if (used_ < (uint64_t)cap_) {
                auto r = (__gm__ volatile PearlProfTrace*)(trace_ +
                    ((uint64_t)worker_ * cap_ + used_) * sizeof(PearlProfTrace));
                r->start = t; r->ticks = dt; r->stage = stage;
                r->band = (uint64_t)(int64_t)band_; r->fold = (uint64_t)(int64_t)fold_;
                r->slot = (uint32_t)slot_ | ((uint64_t)(uint32_t)tile_ << 32);
                r->read_bytes = rd; r->write_bytes = wr;
                ++used_;
            } else { ++dropped_; }
        }
    }
    template<AscendC::HardEvent Event>
    __aicore__ inline void Fence(int32_t id) {
        if (mode_ != 2) return;
        uint64_t t = Begin();
        AscendC::SetFlag<Event>(id);
        AscendC::WaitFlag<Event>(id);
        End(PP_PROFILE_FENCE, t);
    }
    __aicore__ inline uint64_t BeginV() { Fence<AscendC::HardEvent::V_S>(evV_); return Begin(); }
    __aicore__ inline void EndV(int s, uint64_t t, uint64_t rd = 0, uint64_t wr = 0) {
        Fence<AscendC::HardEvent::V_S>(evV_); End(s, t, rd, wr);
    }
    __aicore__ inline uint64_t BeginIn() { Fence<AscendC::HardEvent::MTE2_S>(evIn_); return Begin(); }
    __aicore__ inline void EndIn(uint64_t t, uint64_t n) {
        Fence<AscendC::HardEvent::MTE2_S>(evIn_); End(PP_COPY_IN, t, n, n);
    }
    __aicore__ inline uint64_t BeginOut() { Fence<AscendC::HardEvent::MTE3_S>(evOut_); return Begin(); }
    __aicore__ inline void EndOut(uint64_t t, uint64_t n) {
        Fence<AscendC::HardEvent::MTE3_S>(evOut_); End(PP_EXPORT, t, n, n);
    }
    __aicore__ inline void BarrierV() {
        uint64_t t = Begin(); AscendC::PipeBarrier<PIPE_V>();
        EndV(PP_BARRIER_V, t);
    }
    __aicore__ inline void BarrierAll() {
        uint64_t t = Begin(); AscendC::PipeBarrier<PIPE_ALL>();
        Fence<AscendC::HardEvent::V_S>(evV_);
        Fence<AscendC::HardEvent::MTE2_S>(evIn_);
        Fence<AscendC::HardEvent::MTE3_S>(evOut_);
        End(PP_BARRIER_ALL, t);
    }
    __aicore__ inline void Finish(int nbands, int nfold, int bufs, int vec2x) {
        if (!mode_) return;
        End(PP_TOTAL, start_);
        auto base = stats_ + (uint64_t)worker_ * PEARL_PROF_WORKER_BYTES;
        auto h = (__gm__ volatile uint64_t*)base;
        h[PP_H_MAGIC] = PEARL_PROF_MAGIC; h[PP_H_VERSION] = PEARL_PROF_VERSION;
        h[PP_H_KIND] = kind_; h[PP_H_WORKER] = worker_; h[PP_H_STRIP] = strip_;
        h[PP_H_MODE] = mode_; h[PP_H_START] = start_;
        h[PP_H_END] = start_ + metrics_[PP_TOTAL].ticks;
        h[PP_H_TRACE_COUNT] = used_; h[PP_H_TRACE_DROPPED] = dropped_;
        h[PP_H_TRACE_CAP] = cap_; h[PP_H_NBANDS] = nbands;
        h[PP_H_NFOLD] = nfold; h[PP_H_BUFS] = bufs; h[PP_H_VEC2X] = vec2x;
        h[PP_H_RESERVED] = 0;
        auto out = (__gm__ volatile uint64_t*)(base + PP_HEADER_WORDS * 8);
        for (int s = 0; s < PP_STAGE_COUNT; ++s) {
            out[s * 6] = metrics_[s].calls; out[s * 6 + 1] = metrics_[s].ticks;
            out[s * 6 + 2] = metrics_[s].min_ticks; out[s * 6 + 3] = metrics_[s].max_ticks;
            out[s * 6 + 4] = metrics_[s].read_bytes; out[s * 6 + 5] = metrics_[s].write_bytes;
        }
        for (uint64_t i = 0; i < PP_HEADER_WORDS * 8 + PP_STAGE_COUNT * sizeof(PearlProfMetric); i += 64)
            dcci((__gm__ uint8_t*)(base + i), cache_line_t::SINGLE_CACHE_LINE, dcci_dst_t::CACHELINE_OUT);
        if (trace_) {
            auto tbase = trace_ + (uint64_t)worker_ * cap_ * sizeof(PearlProfTrace);
            for (uint64_t i = 0; i < used_ * sizeof(PearlProfTrace); i += 64)
                dcci((__gm__ uint8_t*)(tbase + i), cache_line_t::SINGLE_CACHE_LINE, dcci_dst_t::CACHELINE_OUT);
        }
    }
private:
    __gm__ uint8_t* stats_; __gm__ uint8_t* trace_;
    int worker_, kind_, strip_, mode_, cap_;
    int band_ = -1, fold_ = -1, slot_ = -1, tile_ = -1;
    int32_t evV_ = 0, evIn_ = 0, evOut_ = 0;
    uint64_t start_ = 0, used_ = 0, dropped_ = 0;
    PearlProfMetric metrics_[PP_STAGE_COUNT];
#else
    __aicore__ inline PearlProfiler(__gm__ uint8_t*, __gm__ uint8_t*, int, int, int, int, int) {}
    __aicore__ inline void InitVectorFences(AscendC::TPipe&) {}
    __aicore__ inline uint64_t Begin() const { return 0; }
    __aicore__ inline uint64_t BeginV() { return 0; }
    __aicore__ inline uint64_t BeginIn() { return 0; }
    __aicore__ inline uint64_t BeginOut() { return 0; }
    __aicore__ inline void Context(int, int, int) {}
    __aicore__ inline void Tile(int) {}
    __aicore__ inline uint64_t BeginPoll() { return 0; }
    __aicore__ inline void EndPoll(int, uint64_t) {}
    __aicore__ inline void Count(int, uint64_t, uint64_t = 0, uint64_t = 0) {}
    __aicore__ inline void End(int, uint64_t, uint64_t = 0, uint64_t = 0) {}
    __aicore__ inline void EndV(int, uint64_t, uint64_t = 0, uint64_t = 0) {}
    __aicore__ inline void EndIn(uint64_t, uint64_t) {}
    __aicore__ inline void EndOut(uint64_t, uint64_t) {}
    __aicore__ inline void BarrierV() { AscendC::PipeBarrier<PIPE_V>(); }
    __aicore__ inline void BarrierAll() { AscendC::PipeBarrier<PIPE_ALL>(); }
    __aicore__ inline void Finish(int, int, int, int) {}
#endif
};
#endif
