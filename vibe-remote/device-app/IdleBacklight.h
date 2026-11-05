#pragma once
#include <cstdint>
namespace vibe {
// Portable backlight policy. Feed physical button edges BEFORE key mapping.
class IdleBacklight {
 public:
  IdleBacklight(uint8_t normal,uint8_t dim,uint32_t delay=30000):normal_(normal),dim_(dim),delay_(delay){}
  void tick(uint32_t now,bool idle) {
    if(!idle||held_){tracking_=false;dimmed_=false;return;}
    if(!tracking_){tracking_=true;since_=now;}
    if(uint32_t(now-since_)>=delay_)dimmed_=true;
  }
  bool button(char key,int phase,uint32_t now) {
    unsigned bit=key=='A'?1:key=='B'?2:key=='C'?4:0;
    if(!bit)return false;
    if(phase==1){held_|=bit;if(dimmed_||suppressed_)suppressed_|=bit;}
    bool consume=(suppressed_&bit)!=0;
    dimmed_=false;tracking_=true;since_=now;
    if(phase!=1){held_&=~bit;suppressed_&=~bit;}
    return consume;
  }
  uint8_t brightness()const{return dimmed_?dim_:normal_;}
  bool dimmed()const{return dimmed_;}
 private:
  uint8_t normal_,dim_;uint32_t delay_,since_=0;unsigned held_=0,suppressed_=0;
  bool tracking_=false,dimmed_=false;
};
}
