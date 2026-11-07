#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
namespace vibe {
// IMA/DVI ADPCM, independent blocks: LE16 predictor, step index, reserved,
// then high-nibble-first codes. The sample count travels in the outer packet.
inline std::string encodeIma(const int16_t* pcm,size_t count,int& index) {
  static constexpr int steps[]={7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
  static constexpr int changes[]={-1,-1,-1,-1,2,4,6,8};
  int predictor=pcm[0];std::string out;out.reserve(4+count/2);
  out+=char(predictor&255);out+=char((predictor>>8)&255);out+=char(index);out+=char(0);
  for(size_t i=1;i<count;++i){
    const int step=steps[index];int diff=int(pcm[i])-predictor,code=diff<0?8:0;if(diff<0)diff=-diff;
    int delta=step>>3;
    if(diff>=step){code|=4;diff-=step;delta+=step;}
    if(diff>=(step>>1)){code|=2;diff-=step>>1;delta+=step>>1;}
    if(diff>=(step>>2)){code|=1;delta+=step>>2;}
    predictor=std::clamp(predictor+((code&8)?-delta:delta),-32768,32767);
    index=std::clamp(index+changes[code&7],0,88);
    if(i&1)out+=char(code<<4);else out.back()=char(static_cast<unsigned char>(out.back())|code);
  }
  return out;
}
}
