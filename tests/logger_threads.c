#include "prl_timing.h"
#include <pthread.h>
#include <assert.h>
static void *work(void *arg) {
    long r=(long)arg;
    prl_context(r,3,(int)r);
    for(int i=0;i<100;++i) prl_record("test.concurrent", 1.25, 1);
    return 0;
}
int main(void) {
    pthread_t t[4];
    for(long i=0;i<4;++i) assert(!pthread_create(t+i,0,work,(void *)i));
    for(int i=0;i<4;++i) assert(!pthread_join(t[i],0));
    prl_summary();
    return 0;
}
