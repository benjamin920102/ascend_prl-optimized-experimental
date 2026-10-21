#ifndef PRL_TIMING_H
#define PRL_TIMING_H
#ifdef __cplusplus
extern "C" {
#endif
int prl_timing_enabled(void);
int prl_timing_device(void);
double prl_now_ms(void);
void prl_context(long round, long strip, int slot);
long prl_round_id(void);
int prl_slot_id(void);
void prl_strip_context(long strip, int slot);
void prl_record(const char *metric, double ms, long value);
void prl_elapsed(const char *metric, double start);
void prl_summary(void);
#ifdef __cplusplus
}
#endif
#endif
