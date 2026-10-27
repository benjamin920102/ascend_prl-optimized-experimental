/* CPU test doubles for instrumentation bookkeeping only, not NPU emulation. */
#pragma once
#include <cstdint>
#define __aicore__
#define __gm__
enum { PIPE_V, PIPE_ALL };
enum class cache_line_t { SINGLE_CACHE_LINE };
enum class dcci_dst_t { CACHELINE_OUT };
static int fakeFlushes = 0, fakeSets = 0, fakeWaits = 0, fakeBarriers = 0;
inline void dcci(void*, cache_line_t, dcci_dst_t) { ++fakeFlushes; }
namespace AscendC {
enum class HardEvent { V_S, MTE2_S, MTE3_S };
inline uint64_t GetSystemCycle() { static uint64_t tick = 100; return tick += 5; }
class TPipe { public: int FetchEventID(HardEvent) { return 1; } };
template<HardEvent E> void SetFlag(int) { ++fakeSets; }
template<HardEvent E> void WaitFlag(int) { ++fakeWaits; }
template<int P> void PipeBarrier() { ++fakeBarriers; }
}
