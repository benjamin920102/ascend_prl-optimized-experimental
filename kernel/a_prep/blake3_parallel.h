#pragma once
#include "blake3_scalar.h"
#include "parallel.h"
#include <stdlib.h>
namespace pearl_b3 {
struct TreeJob {
 const uint8_t *data; size_t len; uint32_t key[8],flags;
 uint32_t *src,*dst;
};
inline void leaf_range(void *v,size_t lo,size_t hi) {
 TreeJob &p=*(TreeJob*)v;
 for(size_t ch=lo;ch<hi;ch++) {
  uint32_t cv[8]; memcpy(cv,p.key,32);
  size_t clen=p.len-ch*1024; if(clen>1024)clen=1024;
  size_t blocks=(clen+63)/64; Output o={};
  for(size_t b=0;b<blocks;b++) {
   uint8_t buf[64]={}; size_t n=clen-b*64; if(n>64)n=64;
   memcpy(buf,p.data+ch*1024+b*64,n); memcpy(o.cv,cv,32);
   for(int j=0;j<16;j++)o.block[j]=load(buf+4*j);
   o.counter=ch; o.len=(uint32_t)n; o.flags=p.flags|(b==0?1:0)|(b+1==blocks?2:0);
   compress(o,cv);
  }
  memcpy(p.dst+ch*8,cv,32);
 }
}
inline void parent_range(void *v,size_t lo,size_t hi) {
 TreeJob &p=*(TreeJob*)v;
 for(size_t i=lo;i<hi;i++)compress(parent(p.src+i*16,p.src+i*16+8,p.key,p.flags),p.dst+i*8);
}
inline void hash_parallel(const uint8_t *data,size_t len,const uint8_t *keyBytes,uint8_t out[32],PearlTeam &team) {
 const size_t chunks=(len+1023)/1024;
 if(chunks<2 || team.workers()==1) { hash(data,len,keyBytes,out); return; }
 uint32_t *a=(uint32_t*)malloc(chunks*32),*b=(uint32_t*)malloc(chunks*32);
 if(!a || !b) { free(a);free(b);hash(data,len,keyBytes,out);return; }
 TreeJob p={};p.data=data;p.len=len;p.flags=keyBytes?16:0;
 for(int j=0;j<8;j++)p.key[j]=keyBytes?load(keyBytes+4*j):iv[j];
 p.dst=a;team.run(chunks,leaf_range,&p);
 size_t n=chunks;
 while(n>2) {
  p.src=a;p.dst=b;
  if(n/2<team.workers()*2)parent_range(&p,0,n/2);
  else team.run(n/2,parent_range,&p);
  if(n&1)memcpy(b+(n/2)*8,a+(n-1)*8,32);
  n=(n+1)/2;uint32_t *tmp=a;a=b;b=tmp;
 }
 uint32_t words[8];compress(parent(a,a+8,p.key,p.flags),words,true);
 for(int j=0;j<32;j++)out[j]=(uint8_t)(words[j/4]>>(8*(j%4)));
 free(a);free(b);
}
}
