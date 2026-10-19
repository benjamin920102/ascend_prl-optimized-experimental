# Runtime slot depth

Rebuild the kernel once: `make kernel RANK=256 MBATCH=1`. Use the same RANK/MBATCH as the existing miner; the Makefile default rank is 256, while standalone CMake defaults differ.

Then prefix the existing launch command with `PRL_BUFS=2`, `PRL_BUFS=4`, `PRL_BUFS=6`, or `PRL_BUFS=8`. For example:

```sh
PRL_BUFS=2 ./scripts/launch.sh
```

All existing wallet, pool, device and CPU settings still apply. Run one configuration at a time and stop the previous process before the next trial. The launch script inherits this environment variable. Containers need `-e PRL_BUFS=2` explicitly.

The variable is read once in pearl_init, before buffer allocation. Unset uses the compiled BUFS default (normally 4). Valid values are decimal integers from 2 through 64, divisible by the compiled VEC2X (normally 2). Invalid values fail initialization. Live changes within an active scan are unsupported; restart the process to select another value.

Cube and Vector receive the same runtime depth. Partial allocation, slot offsets, modulo operations, producer/consumer flags, back-pressure thresholds, flag reset sizes and hang diagnostics all use it. The public C API stays unchanged; both kernel translation units must be rebuilt together.

At GRID=20, NFOLD=16, MBATCH=1, partial memory is 10 MiB per slot of depth (2=20 MiB; 4=40 MiB; 6=60 MiB; 8=80 MiB). This does not imply an equal change in HBM traffic. Smaller depth can increase stalls; larger depth can increase cache pressure. Runtime modulo can also cost more than a compile-time constant, so compare PRL_BUFS=4 against the original build before interpreting changes.

Validation on the development machine: parser tests and host-compiled extraction tests for slot layout/back-pressure passed; patch application checked. No CANN compiler or NPU is available here, so device compilation, full transcript equivalence and throughput must be checked on the target. Keep all other settings fixed and compare complete scan time, not only instantaneous Cube utilization.
