#pragma once
#include <ArduinoJson.h>
#include <functional>
#include <string>
#include <algorithm>
#include <cstdint>
#include "ImaAdpcm.h"

namespace vibe {
// Portable push-to-talk controller. Board supplies PCM16LE/16kHz; SDK details
// stay below this boundary. Every transfer and result is tied to one question.
class VoiceInput {
 public:
  using Send=std::function<void(const JsonDocument&)>;
  using Mic=std::function<bool(bool)>;
  using Overlay=std::function<void(const std::string&,const std::string&)>;
  VoiceInput(int16_t* buffer,size_t capacity,Send send,Mic mic,Overlay overlay)
    :buffer_(buffer),capacity_(std::min(capacity,size_t(160000))),send_(send),mic_(mic),overlay_(overlay){}
  bool active() const{return state_!="idle";}
  const std::string& state() const{return state_;}
  const std::string& session() const{return session_;}
  const std::string& ui() const{return ui_;}
  void start(const std::string& session,const std::string& ui,uint32_t now) {
    cancel();session_=session;ui_=ui;id_="voice-"+std::to_string(++generation_)+"-"+std::to_string(now);
    count_=offset_=seq_=0;adpcmIndex_=0;at_=now;state_="recording";
    if(!buffer_ || !capacity_ || !mic_(true)){error();return;}
    JsonDocument d;packet(d,"voiceStart");d["sessionId"]=session_;d["uiId"]=ui_;d["encoding"]="ima-adpcm";send_(d);
    overlay_("録音中\nAを離すと終了\n最大10秒","MIC ON");
  }
  void audio(const int16_t* data,size_t count,uint32_t now) {
    if(state_!="recording")return;
    const size_t n=std::min(count,capacity_-count_);std::copy_n(data,n,buffer_+count_);count_+=n;
    if(count_==capacity_)stop(now);
  }
  void stop(uint32_t now) {
    if(state_!="recording")return;mic_(false);
    if(count_<3200){error();return;}
    state_="sending";at_=now;waiting_=false;
    overlay_("音声をPCへ\n転送しています","B長:中止");next(now);
  }
  void input(char key,int phase,uint32_t now) {
    if(key=='V' && phase==0){stop(now);return;}
    if(phase!=0)return;
    if(key=='B'){cancel();return;}
    if(key=='C' && (state_=="review" || state_=="error")){start(session_,ui_,now);return;}
    if(key=='A' && state_=="review") {
      JsonDocument d;packet(d,"voiceConfirm");send_(d);state_="submitted";at_=now;
      overlay_("回答をPCへ\n送信しています","送信中");
    }
  }
  void receive(JsonObjectConst m,uint32_t now) {
    const std::string type=m["type"]|"";
    if(type=="ack" && state_=="submitted" && (m["sessionId"]|std::string())==session_ &&
       (m["uiId"]|std::string())==ui_ && (m["actionId"]|std::string())=="voice") {
      if(m["ok"]==true){state_="received";at_=now;overlay_("PCが回答を\n受信しました","受信済み");}else error();
      return;
    }
    if(!active() || (m["id"]|std::string())!=id_)return;
    if(type=="voiceChunkAck" && state_=="sending" && waiting_ && m["seq"].is<unsigned>() && m["seq"].as<unsigned>()==seq_) {
      offset_+=chunk_;++seq_;waiting_=false;next(now);
    } else if(type=="voiceResult" && state_=="transcribing") {
      const std::string text=m["text"]|"";
      if(text.empty() || text.size()>2000){error();return;}
      state_="review";at_=now;overlay_("認識結果\n"+text,"A:送信 B:再録");
    } else if(type=="voiceError")error();
  }
  void tick(uint32_t now,bool valid) {
    if(!active())return;
    if(state_=="received"){if(uint32_t(now-at_)>=2000)cancel();return;}
    if(!valid){cancel();return;}
    if(state_=="recording" && uint32_t(now-at_)>=10000)stop(now);
    else if((state_=="sending" || state_=="submitted") && uint32_t(now-at_)>=10000)error();
    else if((state_=="transcribing" || state_=="review" || state_=="error") && uint32_t(now-at_)>=120000)cancel();
  }
  void cancel() {
    if(!active())return;
    mic_(false);JsonDocument d;packet(d,"voiceCancel");send_(d);
    state_="idle";overlay_("","");count_=0;
  }
 private:
  int16_t* buffer_;size_t capacity_,count_=0,offset_=0,chunk_=0;
  unsigned generation_=0,seq_=0;uint32_t at_=0;bool waiting_=false;int adpcmIndex_=0;
  std::string state_="idle",session_,ui_,id_;
  Send send_;Mic mic_;Overlay overlay_;
  void packet(JsonDocument& d,const char* type){d["type"]=type;d["id"]=id_;}
  void error(){mic_(false);state_="error";overlay_("音声入力エラー\nPCの音声認識と\n接続を確認してください","B:再録 長:戻る");}
  static std::string base64(const uint8_t* p,size_t n) {
    static constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;out.reserve((n+2)/3*4);
    for(size_t i=0;i<n;i+=3){const unsigned a=p[i],b=i+1<n?p[i+1]:0,c=i+2<n?p[i+2]:0;
      out+=alphabet[a>>2];out+=alphabet[((a&3)<<4)|(b>>4)];out+=i+1<n?alphabet[((b&15)<<2)|(c>>6)]:'=';out+=i+2<n?alphabet[c&63]:'=';}
    return out;
  }
  void next(uint32_t now) {
    at_=now;JsonDocument d;
    if(offset_>=count_){packet(d,"voiceEnd");d["chunks"]=seq_;state_="transcribing";overlay_("音声を文字に\n変換しています","B長:中止");}
    else{packet(d,"voiceChunk");chunk_=std::min(size_t(1017),count_-offset_);
      const auto encoded=encodeIma(buffer_+offset_,chunk_,adpcmIndex_);d["seq"]=seq_;d["samples"]=chunk_;
      d["data"]=base64(reinterpret_cast<const uint8_t*>(encoded.data()),encoded.size());waiting_=true;
      overlay_("音声をPCへ\n転送中 "+std::to_string(offset_*100/count_)+"%","B長:中止");}
    send_(d);
  }
};
}
