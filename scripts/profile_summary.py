#!/usr/bin/env python3
"""Summarize standalone source profiling CSV; standard library only."""
import argparse
import csv
import json
from collections import defaultdict
from pathlib import Path


def read_stats(paths):
    rows = []
    required = {"sample", "scan", "strip", "kind", "worker", "stage", "semantics", "mode",
                "calls", "ticks", "min_us", "max_us", "source", "destination", "read_bytes",
                "write_bytes", "trace_dropped", "trace_count"}
    seen = set()
    for path in paths:
        with open(path, newline="", encoding="utf-8") as f:
            reader = csv.DictReader(f)
            if not required <= set(reader.fieldnames or []):
                raise ValueError(f"{path}: missing statistics columns")
            for row in reader:
                key = (str(Path(path).resolve()), row["sample"], row["worker"], row["stage"])
                if key in seen:
                    raise ValueError(f"{path}: repeated worker/stage record {key[1:]}")
                seen.add(key)
                row["file"] = str(Path(path).resolve())
                for k in ("calls", "ticks", "read_bytes", "write_bytes", "trace_dropped", "trace_count"):
                    row[k] = int(row[k])
                    if row[k] < 0:
                        raise ValueError(f"{path}: negative {k}")
                if row["kind"] not in ("cube", "vector") or row["mode"] not in ("1", "2"):
                    raise ValueError(f"{path}: invalid kind/mode")
                row["min_us"], row["max_us"] = float(row["min_us"]), float(row["max_us"])
                rows.append(row)
    if not rows:
        raise ValueError("no profiling rows")
    return rows


def summarize(rows):
    groups = {}
    workers = {}
    for r in rows:
        key = (r["kind"], r["mode"], r["stage"])
        g = groups.setdefault(key, dict(kind=key[0], mode=int(key[1]), stage=key[2], calls=0,
                 ticks=0, min_us=float("inf"), max_us=0, read_bytes=0, write_bytes=0,
                 source=r["source"], destination=r["destination"], semantics=r["semantics"]))
        for name in ("calls", "ticks", "read_bytes", "write_bytes"):
            g[name] += r[name]
        g["min_us"] = min(g["min_us"], r["min_us"])
        g["max_us"] = max(g["max_us"], r["max_us"])
        wk = (r["file"], r["sample"], r["worker"])
        drop = (r["trace_count"], r["trace_dropped"])
        if wk in workers and workers[wk] != drop:
            raise ValueError("inconsistent per-worker trace counters")
        workers[wk] = drop
    for g in groups.values():
        g["total_us"] = g["ticks"] / 50
        g["mean_us"] = g["total_us"] / g["calls"] if g["calls"] else 0
    return dict(stages=list(groups.values()), worker_samples=len(workers),
                trace_events=sum(n for n, _ in workers.values()),
                trace_dropped=sum(n for _, n in workers.values()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", nargs="+", help="*_stats.csv files (not *_host.csv)")
    parser.add_argument("--json", type=Path, help="also write a machine-readable summary")
    args = parser.parse_args()
    try:
        report = summarize(read_stats(args.csv))
    except (OSError, ValueError) as e:
        parser.error(str(e))
    print("GetSystemCycle: 50 MHz (20 ns/tick). All bytes are LOGICAL operands.")
    print("Mode 1: scalar/API regions; mode 2: exposed Vector/MTE regions include added fences.")
    print("Totals sum workers. Inclusive/nested regions overlap; do not add all stage times.")
    print("Cube IterateAll includes library work; L1/L0/MTE1/FIXPIPE internals are not measured separately.")
    for kind, mode in sorted({(s["kind"], s["mode"]) for s in report["stages"]}):
        stages = [s for s in report["stages"] if (s["kind"], s["mode"]) == (kind, mode)]
        total = next((s["total_us"] for s in stages if s["stage"] == "kernel_total"), 0)
        wait = next((s["total_us"] for s in stages if s["stage"] == "flag_spin_wait"), 0)
        print(f"\n{kind} mode={mode}: summed kernel={total:.3f} us, flag polling wait={wait:.3f} us"
              + (f" ({wait / total:.2%} of summed kernel)" if total else ""))
        print(f"{'stage':30} {'calls':>12} {'sum_us':>13} {'mean_us':>12} {'max_us':>12}")
        for s in sorted(stages, key=lambda s: s["ticks"], reverse=True):
            print(f"{s['stage']:30} {s['calls']:12d} {s['total_us']:13.3f} {s['mean_us']:12.6f} {s['max_us']:12.3f}")
        flows = defaultdict(lambda: [0, 0])
        for s in stages:
            if s["read_bytes"] or s["write_bytes"]:
                flow = flows[(s["source"], s["destination"])]
                flow[0] += s["read_bytes"]; flow[1] += s["write_bytes"]
        print("Logical dataflow (read/write bytes include each vector instruction's operands):")
        for (src, dst), (rd, wr) in sorted(flows.items()):
            print(f"  {src:6} -> {dst:6} read={rd:14,d} B  write={wr:14,d} B")
    print(f"\nWorker samples: {report['worker_samples']}; trace events: {report['trace_events']}; dropped: {report['trace_dropped']}")
    if report["trace_dropped"]:
        print("Trace is truncated. CSV summary includes all instrumented calls.")
    if args.json:
        args.json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
