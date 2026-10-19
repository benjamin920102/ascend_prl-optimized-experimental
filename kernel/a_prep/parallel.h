#pragma once
#include <pthread.h>
#include <unistd.h>
#include <stddef.h>

// One bounded team per preparation, reused across all dependency stages.
// numBlocks remains one: AICPU's launch dimension is not a worker count.
class PearlTeam {
public:
 using Fn = void (*)(void *, size_t, size_t);
 explicit PearlTeam(unsigned requested) {
  long online = sysconf(_SC_NPROCESSORS_ONLN);
  online_=(int)online;
  unsigned n = requested ? requested : (online > 0 ? (unsigned)online : 1);
  if(n > 64) n = 64;
  if(!n) n = 1;
  effective_=n;
  for(unsigned i=0;i+1<n;i++) {
   states_[i]={this,i};
   int rc=pthread_create(&ids_[i],nullptr,entry,&states_[i]);
   if(rc) { createError_=rc; break; }
   ++created_;
  }
  workers_=created_+1;
 }
 ~PearlTeam() {
  pthread_mutex_lock(&mu_); stop_=true; pthread_cond_broadcast(&wake_); pthread_mutex_unlock(&mu_);
  for(unsigned i=0;i<created_;i++) pthread_join(ids_[i],nullptr);
  pthread_cond_destroy(&wake_); pthread_cond_destroy(&done_); pthread_mutex_destroy(&mu_);
 }
 unsigned workers() const { return workers_; }
 unsigned effective() const { return effective_; }
 int create_error() const { return createError_; }
 int online_cpus() const { return online_; }
 void run(size_t count,Fn fn,void *ctx) {
  // A task can be expensive (e.g. one PRNG partition); even a few tasks
  // must run in parallel. Tiny tree reductions are handled by their caller.
  if(workers_==1 || count<2) { fn(ctx,0,count); return; }
  pthread_mutex_lock(&mu_);
  count_=count; fn_=fn; ctx_=ctx; pending_=created_; ++epoch_;
  pthread_cond_broadcast(&wake_); pthread_mutex_unlock(&mu_);
  fn(ctx,count*(workers_-1)/workers_,count);
  pthread_mutex_lock(&mu_);
  while(pending_) pthread_cond_wait(&done_,&mu_);
  pthread_mutex_unlock(&mu_);
 }
 PearlTeam(const PearlTeam&)=delete;
 PearlTeam& operator=(const PearlTeam&)=delete;
private:
 struct State { PearlTeam *team; unsigned id; };
 static void *entry(void *v) {
  State &s=*(State*)v; PearlTeam &p=*s.team; unsigned seen=0;
  pthread_mutex_lock(&p.mu_);
  for(;;) {
   while(!p.stop_ && seen==p.epoch_) pthread_cond_wait(&p.wake_,&p.mu_);
   if(p.stop_) break;
   seen=p.epoch_; Fn fn=p.fn_; void *ctx=p.ctx_;
   size_t lo=p.count_*s.id/p.workers_,hi=p.count_*(s.id+1)/p.workers_;
   pthread_mutex_unlock(&p.mu_); if(lo<hi)fn(ctx,lo,hi); pthread_mutex_lock(&p.mu_);
   if(!--p.pending_) pthread_cond_signal(&p.done_);
  }
  pthread_mutex_unlock(&p.mu_); return nullptr;
 }
 pthread_mutex_t mu_=PTHREAD_MUTEX_INITIALIZER;
 pthread_cond_t wake_=PTHREAD_COND_INITIALIZER,done_=PTHREAD_COND_INITIALIZER;
 pthread_t ids_[63]; State states_[63];
 unsigned created_=0,workers_=1,epoch_=0,pending_=0,effective_=1;
 int createError_=0,online_=0;
 size_t count_=0; Fn fn_=nullptr; void *ctx_=nullptr; bool stop_=false;
};
