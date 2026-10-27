"""Host-only ABI, bounded trace, collection and summary tests. No hardware claims."""
import csv
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("profile_summary", ROOT / "scripts/profile_summary.py")
summary = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(summary)


@unittest.skipUnless(shutil.which("g++"), "g++ required")
class InstrumentationTests(unittest.TestCase):
    def compile_run(self, source, mode=1, args=()):
        with tempfile.TemporaryDirectory() as td:
            cpp = Path(td) / "test.cpp"
            binary = Path(td) / "test"
            cpp.write_text(source)
            built = subprocess.run(["g++", "-std=c++14", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                            "-Wno-unused-variable", f"-DPEARL_PROFILE={mode}", "-I", str(ROOT),
                            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
                            str(cpp), "-o", str(binary)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")
            ran = subprocess.run([str(binary), *map(str, args)], capture_output=True, text=True, env=env)
            self.assertEqual(ran.returncode, 0, ran.stderr)
            return ran

    def test_device_trace_overflow_has_full_summary_and_isolation(self):
        self.compile_run(r'''
        #include "tests/profile_fake_device.h"
        #include "kernel/pearl_profile_device.h"
        #include <cassert>
        #include <vector>
        int main() {
            const int cap = 2, worker = 1;
            std::vector<uint64_t> stats(PEARL_PROF_WORKER_BYTES * 3 / 8, UINT64_C(0xdeadbeef));
            std::vector<uint64_t> trace(3 * cap * sizeof(PearlProfTrace) / 8, UINT64_C(0xdeadbeef));
            PearlProfiler p((uint8_t*)stats.data(), (uint8_t*)trace.data(), worker, PP_VECTOR, 7, 1, cap);
            p.Context(13, 3, 1);
            for (int i = 0; i < 3; ++i) { auto t = p.Begin(); p.End(PP_ADD, t, 64, 32); }
            p.Count(PP_FLAG_POLLS, 9, 36); p.Count(PP_BACKOFF, 40);
            p.Finish(83, 32, 4, 2);
            auto h = stats.data() + PEARL_PROF_WORKER_BYTES / 8;
            auto m = (PearlProfMetric*)(h + PP_HEADER_WORDS);
            assert(h[PP_H_MAGIC] == PEARL_PROF_MAGIC && h[PP_H_WORKER] == 1);
            assert(h[PP_H_TRACE_COUNT] == 2 && h[PP_H_TRACE_DROPPED] == 1);
            assert(m[PP_ADD].calls == 3 && m[PP_ADD].ticks == 15);
            assert(m[PP_ADD].read_bytes == 192 && m[PP_ADD].write_bytes == 96);
            assert(m[PP_ADD].min_ticks == 5 && m[PP_ADD].max_ticks == 5);
            assert(m[PP_FLAG_POLLS].calls == 9 && m[PP_FLAG_POLLS].read_bytes == 36);
            assert(m[PP_TOTAL].calls == 1 && m[PP_TOTAL].ticks > 0);
            assert(stats[0] == UINT64_C(0xdeadbeef));
            assert(stats[2 * PEARL_PROF_WORKER_BYTES / 8] == UINT64_C(0xdeadbeef));
            auto r = (PearlProfTrace*)trace.data();
            assert(r[cap].band == 13 && r[cap].fold == 3 && (int32_t)r[cap].slot == 1);
            assert((int32_t)(r[cap].slot >> 32) == -1);
            assert(r[cap].stage == PP_ADD && r[cap + 1].stage == PP_ADD);
            assert(trace[0] == UINT64_C(0xdeadbeef));
            assert(trace[2 * cap * sizeof(PearlProfTrace) / 8] == UINT64_C(0xdeadbeef));
            assert(fakeSets == 0 && fakeWaits == 0 && fakeFlushes > 0);
        }
        ''')

    def test_mode_two_fences_and_disabled_runtime(self):
        self.compile_run(r'''
        #include "tests/profile_fake_device.h"
        #include "kernel/pearl_profile_device.h"
        #include <cassert>
        #include <vector>
        int main() {
            std::vector<uint64_t> stats(PEARL_PROF_WORKER_BYTES / 8);
            AscendC::TPipe pipe;
            PearlProfiler disabled(nullptr, nullptr, 0, PP_VECTOR, 0, 2, 0);
            disabled.InitVectorFences(pipe); auto t = disabled.BeginV(); disabled.EndV(PP_ADD, t);
            disabled.BarrierV(); disabled.BarrierAll(); disabled.Finish(1, 32, 4, 2);
            assert(fakeSets == 0 && fakeWaits == 0 && fakeFlushes == 0 && fakeBarriers == 2);
            PearlProfiler p((uint8_t*)stats.data(), nullptr, 0, PP_VECTOR, 0, 2, 0);
            p.InitVectorFences(pipe); t = p.BeginV(); p.EndV(PP_ADD, t, 64, 32);
            t = p.BeginIn(); p.EndIn(t, 32); t = p.BeginOut(); p.EndOut(t, 16);
            p.BarrierV(); p.BarrierAll(); p.Finish(1, 32, 4, 2);
            auto m = (PearlProfMetric*)(stats.data() + PP_HEADER_WORDS);
            assert(fakeSets == fakeWaits && fakeSets == 10);
            assert(m[PP_PROFILE_FENCE].calls == 10);
            assert(m[PP_COPY_IN].read_bytes == 32 && m[PP_EXPORT].write_bytes == 16);
            assert(m[PP_ADD].ticks >= m[PP_PROFILE_FENCE].max_ticks);
        }
        ''')

    def test_compile_time_off_has_no_device_timer_or_fences(self):
        self.compile_run(r'''
        #include "tests/profile_fake_device.h"
        #include "kernel/pearl_profile_device.h"
        #include <cassert>
        int main() {
            PearlProfiler p(nullptr, nullptr, 0, PP_VECTOR, 0, 2, 1024);
            AscendC::TPipe pipe; p.InitVectorFences(pipe);
            assert(p.Begin() == 0 && p.BeginV() == 0 && p.BeginIn() == 0 && p.BeginOut() == 0);
            p.End(PP_HASH, 0); p.EndV(PP_ADD, 0); p.EndIn(0, 32); p.EndOut(0, 32);
            p.Context(1, 2, 3); p.Count(PP_FLAG_POLLS, 8); p.Finish(1, 32, 4, 2);
            p.BarrierV(); p.BarrierAll();
            assert(fakeSets == 0 && fakeWaits == 0 && fakeFlushes == 0 && fakeBarriers == 2);
        }
        ''', mode=0)

    def test_host_collector_csv_trace_and_sampling(self):
        source = r'''
        #include <iostream>
        #include <cstdlib>
        #include <cstring>
        #include "tests/profile_fake_device.h"
        #include "src/pearl_profile_schema.h"
        #define PEARL_PIPE_DEPTH 2
        #define NINNER 1
        #define NFOLD 32
        #define VEC2X 2
        #define BUFS 4
        const int GRID=2, PART_ELEMS=8192, WORDS=16, BN=64, MM_N=128;
        int g_dev=0, g_nbands=83;
        using aclrtEvent = void*;
        enum { ACL_MEM_MALLOC_HUGE_FIRST, ACL_EVENT_SYNC };
        int aclrtMalloc(void** p, size_t n, int) { *p = calloc(1,n); return *p ? 0 : 1; }
        int aclrtMallocHost(void** p, size_t n) { return aclrtMalloc(p,n,0); }
        int aclrtFree(void* p) { free(p); return 0; }
        int aclrtFreeHost(void* p) { return aclrtFree(p); }
        int aclrtCreateEventExWithFlag(void** p, int) { *p = malloc(1); return 0; }
        int aclrtDestroyEvent(void* p) { return aclrtFree(p); }
        void CheckAcl(int e, const char*) { if (e) exit(2); }
        #include "src/pearl_profile_host.h"
        #include "kernel/pearl_profile_device.h"
        #include <cassert>
        int main(int argc, char** argv) {
            setenv("PRL_PROF_OUT", argv[1], 1);
            if (argc > 2) {
                unsetenv("PRL_PROF"); unsetenv("PRL_PROF_TRACE");
                unsetenv("PRL_PROF_STRIPS"); unsetenv("PRL_PROF_EVERY");
                ProfInit();
                assert(g_profMode == 2 && g_profCap == 4096 && g_profLimit == 4 && g_profEvery == 1);
                // An ordinary launch can submit without any profiling settings.
                assert(ProfSubmit(0)); g_profActive[0] = false;
                ProfShutdown(); return 0;
            }
            setenv("PRL_PROF", "1", 1); setenv("PRL_PROF_TRACE", "1", 1);
            setenv("PRL_PROF_STRIPS", "2", 1); setenv("PRL_PROF_EVERY", "2", 1);
            ProfInit(); assert(ProfSubmit(0)); ProfEnqueued(0);
            for (int w=0; w<PROF_WORKERS; ++w) {
                PearlProfiler p(g_profD[0], g_traceD[0], w, w<GRID ? PP_CUBE : PP_VECTOR, 3, 1, 1);
                p.Context(w, 1, 0); auto t=p.Begin(); p.End(PP_ADD, t, 64, 32);
                t=p.Begin(); p.End(PP_HASH, t); p.Finish(83,32,4,2);
            }
            memcpy(g_profH[0], g_profD[0], PROF_STATS_BYTES);
            memcpy(g_traceH[0], g_traceD[0], g_profTraceBytes);
            ProfCollect(0, ProfHostNow()); assert(!g_profActive[0]);
            assert(!ProfSubmit(1)); assert(ProfSubmit(1)); g_profActive[1]=false;
            assert(!ProfSubmit(0)); assert(!ProfSubmit(0));
            ProfShutdown();
        }
        '''
        with tempfile.TemporaryDirectory() as out:
            self.compile_run(source, args=(out,))
            stats = list(Path(out).glob("*_stats.csv"))
            report = summary.summarize(summary.read_stats(stats))
            self.assertEqual(report["worker_samples"], 6)
            self.assertEqual(report["trace_events"], 6)
            self.assertEqual(report["trace_dropped"], 6)
            data = json.loads(next(Path(out).glob("*_trace.json")).read_text())
            self.assertEqual(len(data["traceEvents"]), 12)
            self.assertTrue(all(e["dur"] >= 0 and e["ts"] >= 0 for e in data["traceEvents"]))
            metadata = json.loads(next(Path(out).glob("*_metadata.json")).read_text())
            self.assertEqual(metadata["clock_MHz"], 50)
            self.assertEqual(metadata["declared_vector_UB_bytes"], 151552)
            cli = subprocess.run(["python3", str(ROOT / "scripts/profile_summary.py"), str(stats[0])],
                                 check=True, capture_output=True, text=True)
            self.assertIn("Trace is truncated", cli.stdout)
            with next(Path(out).glob("*_host.csv")).open() as f:
                host = list(csv.DictReader(f))
            self.assertEqual(len(host), 1)
            self.assertGreaterEqual(float(host[0]["host_wait_event_us"]), 0)
        with tempfile.TemporaryDirectory() as out:
            self.compile_run(source, args=(out, "defaults"))
            metadata = json.loads(next(Path(out).glob("*_metadata.json")).read_text())
            self.assertEqual(metadata["mode"], 2)
            self.assertEqual(metadata["trace_capacity_per_worker"], 4096)
            self.assertEqual(metadata["sample_limit"], 4)


class SummaryTests(unittest.TestCase):
    def test_missing_columns_and_empty_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "bad.csv"
            p.write_text("worker,ticks\n0,4\n")
            with self.assertRaisesRegex(ValueError, "missing statistics"):
                summary.read_stats([p])
        with self.assertRaisesRegex(ValueError, "no profiling"):
            summary.read_stats([])


if __name__ == "__main__":
    unittest.main()
