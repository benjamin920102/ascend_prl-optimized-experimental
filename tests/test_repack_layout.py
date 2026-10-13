#!/usr/bin/env python3
"""Host-only math check: per-fold transpose packing, no CANN dependency.
This is NOT a hardware bitwise golden-proof verification.
"""
import random


def check_fold_layout(seed=12, n=8, fold_k=16, folds=4, rows=8, m=6):
    rng = random.Random(seed)
    ktotal = fold_k * folds
    # Natural `[N,KFULL]` A-side matrix used by original transpose-B matmul.
    a = [rng.randint(-60, 60) for _ in range(n * ktotal)]
    # Logical left operand `[M,KFULL]`.
    b = [rng.randint(-60, 60) for _ in range(m * ktotal)]
    # Repack into contiguous `[KFULL,N]` as in repackAT().
    at = [0] * (n * ktotal)
    for gk in range(ktotal):
        for r in range(n):
            at[gk * n + r] = a[r * ktotal + gk]
    for p in range(folds):
        for mi in range(m):
            for ri in range(n):
                golden = sum(b[mi * ktotal + p * fold_k + ki]
                             * a[ri * ktotal + p * fold_k + ki]
                             for ki in range(fold_k))
                got_packed = sum(b[mi * ktotal + p * fold_k + ki]
                                 * at[(p * fold_k + ki) * n + ri]
                                 for ki in range(fold_k))
                assert golden == got_packed, (p, mi, ri, golden, got_packed)
    # Counterexample for silently narrowing the sum to signed int16.
    assert sum([127 * 127] * 128) > 32767
    # Fold prefixes are semantically distinct, not replaceable by their total.
    assert (17 ^ (17 + 42)) != (17 + 42)


if __name__ == "__main__":
    for seed in range(20):
        check_fold_layout(seed=seed)
    print("PASS: K-major packing matches natural layout across tested toy folds")
    print("NOTE: does not compile Ascend C or verify real 910B output")
