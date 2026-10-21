#!/usr/bin/env python3
"""Summarize timing CSVs using Python's standard library (no external packages)."""
import argparse
import csv
import math
from collections import defaultdict

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv', nargs='+')
    parser.add_argument('--skip-rounds', type=int, default=1,
                        help='exclude rounds < N per process; default 1 excludes warmup round 0')
    args = parser.parse_args()
    samples = defaultdict(list)
    counts = defaultdict(int)
    for path in args.csv:
        with open(path, newline='') as file:
            for row in csv.DictReader(file):
                if int(row['round']) < args.skip_rounds:
                    continue
                samples[row['metric']].append(float(row['ms']))
                counts[row['metric']] += int(row['value'])
    print(f'{"metric":36s} {"n":>8s} {"total_ms":>13s} {"avg_ms":>12s} {"p50_ms":>12s} {"p95_ms":>12s} {"max_ms":>12s} {"sum_value":>12s}')
    for name, values in sorted(samples.items()):
        values.sort()
        n = len(values)
        q = lambda fraction: values[max(0, math.ceil(n*fraction)-1)]
        print(f'{name:36s} {n:8d} {sum(values):13.3f} {sum(values)/n:12.3f} {q(.5):12.3f} {q(.95):12.3f} {values[-1]:12.3f} {counts[name]:12d}')
    print('Overlapping durations are inclusive and must not be added together.')
    print('Counter metrics use value (not ms); sum_value of strip.inflight is not an occupancy average.')
if __name__ == '__main__':
    main()
