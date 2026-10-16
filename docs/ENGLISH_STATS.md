# English status output

Estimated hashrate = M * N * K * scan rate / 1e12, using the project work convention (one multiply-accumulate per equivalent hash). MBATCH groups strips within a scan; do not multiply the rate by MBATCH again. Rates are cumulative from startup and include reconnect and preparation time. Hits can terminate a scan early, so this is an estimate, not a pool-side effective hashrate.

Submitted counts share send attempts, including interrupted sends; an unacknowledged attempt is not an accepted share. Only outstanding mining.submit IDs update Accepted or Rejected; authorization, duplicate and unrelated responses do not. Reconnect clears outstanding reply tracking without resetting lifetime counters. At most 256 outstanding submissions are tracked.

Rebuild both the miner and kernel on the Ascend toolchain. This archive contains source, not verified 910B binaries.
