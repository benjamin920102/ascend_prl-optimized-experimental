#!/usr/bin/env python3
"""Execute the actual SIMD helper through a synchronous CPU API shim.
Checks arithmetic/layout, NOT CANN types, NPU synchronization or performance.
Requires g++ and Python standard library only; build files are temporary.
"""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
vec = (root/'kernel/pearl_hlc2_vec.asc').read_text()
constants = vec[vec.index('constexpr int32_t BLAKE3_OUT_LEN'):vec.index('__aicore__ inline uint64_t Slot')]
scalar = (root/'tests/scalar_blake3_reference.h').read_text()
shim = r'''
#include <cstdint>
#include <cstring>
#include <vector>
#include <random>
#include <iostream>
#include <cassert>
#define __aicore__
constexpr int PIPE_V = 0;
namespace AscendC {
template<class T> struct LocalTensor {
    unsigned char* p;
    LocalTensor operator[](int i) const {return {p+i*sizeof(T)};}
    template<class U> LocalTensor<U> ReinterpretCast() const {return {p};}
    T get(int i) const {T t; std::memcpy(&t,p+i*sizeof(T),sizeof(T)); return t;}
    void put(int i,T t) const {std::memcpy(p+i*sizeof(T),&t,sizeof(T));}
};
template<int P> void PipeBarrier() {}
template<class T> void Or(LocalTensor<T> d,LocalTensor<T> a,LocalTensor<T> b,int n) {
 for(int i=0;i<n;++i)d.put(i,T(a.get(i)|b.get(i)));}
template<class T> void And(LocalTensor<T> d,LocalTensor<T> a,LocalTensor<T> b,int n) {
 for(int i=0;i<n;++i)d.put(i,T(a.get(i)&b.get(i)));}
template<class T> void Sub(LocalTensor<T> d,LocalTensor<T> a,LocalTensor<T> b,int n) {
 for(int i=0;i<n;++i)d.put(i,T(a.get(i)-b.get(i)));}
template<class T> void Add(LocalTensor<T> d,LocalTensor<T> a,LocalTensor<T> b,int n) {
 for(int i=0;i<n;++i){uint32_t x=uint32_t(a.get(i))+uint32_t(b.get(i));
 T t; std::memcpy(&t,&x,sizeof(T)); d.put(i,t);}}
template<class T> void Duplicate(LocalTensor<T> d,T t,int n) {for(int i=0;i<n;++i)d.put(i,t);}
void ShiftLeft(LocalTensor<uint32_t> d,LocalTensor<uint32_t> a,uint32_t b,int n) {
 for(int i=0;i<n;++i)d.put(i,a.get(i)<<b);}
void ShiftRight(LocalTensor<uint32_t> d,LocalTensor<uint32_t> a,uint32_t b,int n) {
 for(int i=0;i<n;++i)d.put(i,a.get(i)>>b);}
}
'''
main = r'''
int main() {
 std::mt19937 rng(20261102);
 uint32_t input[16*64],state[16*64],t0[64],t1[64],key[8];
 auto tensor=[](uint32_t* p){return AscendC::LocalTensor<uint32_t>{reinterpret_cast<unsigned char*>(p)};};
 for(int sample=0;sample<103;++sample) {
  for(auto& k:key)k=rng();
  for(auto& x:input)x=sample==0?0:sample==1?UINT32_MAX:sample==2?0x80000000u:rng();
  pearl_simd::Hash(tensor(state),tensor(input),key,tensor(t0),tensor(t1));
  for(int hi=0;hi<64;++hi) {
   uint32_t block[16],expected[8];
   for(int w=0;w<16;++w)block[w]=input[w*64+hi];
   blake3_hash_keyed(expected,block,key,64);
   for(int w=0;w<8;++w)assert(expected[w]==state[w*64+hi]);
  }
 }
 std::cout<<"PASS: 6592 lane hashes match existing scalar BLAKE3 (random and edge inputs)\n";
}
'''
with tempfile.TemporaryDirectory() as td:
    cpp=Path(td)/'check.cpp'; binary=Path(td)/'check'
    cpp.write_text(shim+constants+scalar+'\n#include "pearl_blake3_simd.h"\n'+main)
    subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Wno-unknown-pragmas',
                    '-fsanitize=undefined','-I'+str(root/'kernel'),str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
