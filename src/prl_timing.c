/* One logger shared by miner and kernel host library. No accelerator calls. */
#include "prl_timing.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>
static pthread_once_t once = PTHREAD_ONCE_INIT;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static FILE *csv;
static int enabled, printing, device;
static __thread long ctx_round = -1, ctx_strip = -1;
static __thread int ctx_slot = -1;
typedef struct { char name[80]; unsigned long count; double total, min, max; } stat_t;
static stat_t stats[128];
static int nstats;
double prl_now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}
static void finish(void) {
    prl_summary();
    pthread_mutex_lock(&mu);
    if (csv) { fclose(csv); csv = NULL; }
    pthread_mutex_unlock(&mu);
}
static void init(void) {
    const char *v = getenv("PRL_TIMING"); enabled = !v || strcmp(v, "0");
    v = getenv("PRL_TIMING_PRINT"); printing = !v || strcmp(v, "0");
    v = getenv("PRL_TIMING_DEVICE"); device = enabled && (!v || strcmp(v, "0"));
    if (!enabled) return;
    char name[128]; snprintf(name, sizeof name, "prl_timing_%ld.csv", (long)getpid());
    v = getenv("PRL_TIMING_CSV");
    if (!v || strcmp(v, "0")) {
        /* Exclusive creation avoids silently overwriting or mixing prior runs. */
        csv = fopen(v && *v ? v : name, "wx");
        if (!csv) fprintf(stderr, "[timing] cannot create CSV %s; stderr/summary remain available\n", v && *v ? v : name);
        else {
            setvbuf(csv, NULL, _IOLBF, 0);
            fputs("end_monotonic_ms,pid,tid,round,strip,slot,metric,ms,value\n", csv);
            fprintf(stderr, "[timing] CSV=%s device=%d\n", v && *v ? v : name, device);
        }
    }
    atexit(finish);
}
int prl_timing_enabled(void) { pthread_once(&once, init); return enabled; }
int prl_timing_device(void) { pthread_once(&once, init); return device; }
void prl_context(long r, long s, int b) { ctx_round=r; ctx_strip=s; ctx_slot=b; }
long prl_round_id(void) { return ctx_round; }
int prl_slot_id(void) { return ctx_slot; }
void prl_strip_context(long s, int b) { ctx_strip=s; ctx_slot=b; }
void prl_record(const char *metric, double ms, long value) {
    if (!prl_timing_enabled()) return;
    double end = prl_now_ms();
    pthread_mutex_lock(&mu);
    int i;
    for (i=0; i<nstats; ++i) if (!strcmp(stats[i].name, metric)) break;
    if (i == nstats && nstats < 128) {
        snprintf(stats[i].name, sizeof stats[i].name, "%s", metric);
        stats[i].min=ms; ++nstats;
    }
    if (i < nstats) {
        stat_t *s=&stats[i]; ++s->count; s->total+=ms;
        if(ms<s->min) s->min=ms;
        if(ms>s->max) s->max=ms;
    }
    if (printing) fprintf(stderr, "[timing] round=%ld strip=%ld slot=%d %-30s %10.3f ms value=%ld\n", ctx_round,ctx_strip,ctx_slot,metric,ms,value);
    if (csv) fprintf(csv, "%.6f,%ld,%ld,%ld,%ld,%d,%s,%.6f,%ld\n",end,(long)getpid(),(long)syscall(SYS_gettid),ctx_round,ctx_strip,ctx_slot,metric,ms,value);
    pthread_mutex_unlock(&mu);
}
void prl_elapsed(const char *m, double start) { prl_record(m,prl_now_ms()-start,0); }
void prl_summary(void) {
    if (!prl_timing_enabled()) return;
    pthread_mutex_lock(&mu);
    fputs("[timing-summary] cumulative; overlapping durations must not be added\n", stderr);
    for(int i=0;i<nstats;++i) {
        stat_t *s=&stats[i];
        fprintf(stderr,"[timing-summary] %-30s n=%lu total=%.3f avg=%.3f min=%.3f max=%.3f ms\n",s->name,s->count,s->total,s->total/s->count,s->min,s->max);
    }
    pthread_mutex_unlock(&mu);
}
