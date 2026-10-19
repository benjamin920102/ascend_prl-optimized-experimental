#pragma once
#include <stdint.h>
struct PearlPrepStats {
 uint32_t effective_workers,actual_workers;
 int32_t create_error,online_cpus,affinity_cpus,affinity_error,current_cpu;
 uint32_t reserved;
};
static_assert(sizeof(PearlPrepStats)==32,"shared stats layout");
struct PearlPrepArgs {
 uint64_t seed,m,k;
 uint32_t rank,cert,random_fill,threads;
 uint32_t worker_threads,profile,make_an; // 0=auto, otherwise 1..64 device pthread workers
 uint8_t key[32],commit_b[32];
 int8_t *a,*eal,*an;
 uint16_t *first,*second;
 uint8_t *roots; // rootA || commitA || PearlPrepStats (32 bytes)
};
