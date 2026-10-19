#pragma once
#include <stdint.h>
struct PearlPrepArgs {
 uint64_t seed,m,k;
 uint32_t rank,cert,random_fill,threads;
 uint32_t worker_threads,profile,make_an; // 0=auto, otherwise 1..64 device pthread workers
 uint8_t key[32],commit_b[32];
 int8_t *a,*eal,*an;
 uint16_t *first,*second;
 uint8_t *roots; // rootA || commitA || actual worker count (LE u32)
};
