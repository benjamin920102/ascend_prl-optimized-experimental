/* Mock host queue: proves timing does not change strip order or hit draining. */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "pearl_pipeline.h"
#include "prl_timing.h"
static int sub, done, hit_at, max_inflight;
static const int8_t *base;
extern int scan_full(const int8_t *,int,const uint8_t *,const uint32_t *,int,int,int,uint32_t *,int *,int *);
extern ssize_t encode_proof_b64(const uint8_t *,size_t,int,uint8_t *,size_t);
extern ssize_t original_encode(const uint8_t *,size_t,int,uint8_t *,size_t);
void pearl_set_key_target(const uint32_t *k,const uint32_t *t) { (void)k;(void)t; }
void pearl_set_nhi(int n) { assert(n==2); }
void pearl_strip_submit_fast(const int8_t *a) {
    assert(a==base+(size_t)sub*128*4096);
    assert(sub-done<PEARL_PIPE_DEPTH);
    ++sub;
    if (sub-done>max_inflight) max_inflight=sub-done;
}
void pearl_strip_wait_done(void) { assert(done<sub); ++done; }
int pearl_get_hit_result(int *s,int *t) {
    if (done-1!=hit_at) return 0;
    *s=hit_at; *t=13; return 1;
}
static void scan_case(int n,int hit) {
    sub=done=max_inflight=0; hit_at=hit;
    int hs=-1,ht=-1;
    uint8_t key[32]={0}; uint32_t target[8]={0};
    int rc=scan_full(base,n,key,target,1,2,1,NULL,&hs,&ht);
    assert(rc==(hit>=0)); assert(done==sub);
    assert(max_inflight<=PEARL_PIPE_DEPTH);
    if(hit>=0) {assert(hs==hit && ht==13); assert(sub>=hit+1);}
    else assert(sub==n);
}
int main(void) {
    base=calloc(12,128*4096); assert(base);
    prl_context(2,-1,0);
    scan_case(0,-1); scan_case(1,-1); scan_case(12,-1);
    scan_case(12,0); scan_case(12,4); scan_case(12,11);
    uint8_t in[8192],a[20000],b[20000];
    for(size_t i=0;i<sizeof in;++i) in[i]=(uint8_t)(i*17);
    for(int gz=0;gz<=1;++gz) for(size_t n=0;n<=sizeof in;n+=127) {
        ssize_t na=encode_proof_b64(in,n,gz,a,sizeof a);
        ssize_t nb=original_encode(in,n,gz,b,sizeof b);
        assert(na==nb && na>=0 && !memcmp(a,b,(size_t)na));
    }
    assert(encode_proof_b64(in,10,0,a,1)==-1);
    free((void *)base);
    return 0;
}
