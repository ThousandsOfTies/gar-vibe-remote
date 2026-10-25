#include "CompactUi.h"
#include <algorithm>
#include <cstring>
LV_FONT_DECLARE(vibe_jp_16);

namespace vibe {
namespace {
std::string text(JsonVariantConst v, const char* fallback="") { return v.is<const char*>() ? v.as<const char*>() : fallback; }
uint32_t frame(const std::string& s) { return s=="running" ? 0x238a43 : (s=="waiting" || s=="failed") ? 0x750e23 : 0x4d5552; }
const char* stateName(const std::string& s) { return s=="running" ? "実行中" : s=="waiting" ? "入力待ち" : s=="done" ? "完了" : s=="failed" ? "エラー" : "待機中"; }
std::string shorten(std::string s, int width) {
  const auto fits=[width](const std::string& value){return _lv_txt_get_width(value.c_str(),value.size(),&vibe_jp_16,0,LV_TXT_FLAG_NONE)<=width;};
  if (fits(s)) return s;
  while (!s.empty()) {
    size_t p=s.size()-1; while(p && (static_cast<unsigned char>(s[p])&0xc0)==0x80) --p;
    s.resize(p); if(fits(s+"...")) return s+"...";
  }
  return "...";
}
}
lv_obj_t* CompactUi::label(int x,int y,int w,const lv_font_t* font) {
  auto* o=lv_label_create(root_,nullptr);
  lv_label_set_long_mode(o,LV_LABEL_LONG_CROP); lv_obj_set_width(o,w); lv_obj_set_pos(o,x,y);
  lv_obj_set_style_local_text_font(o,LV_LABEL_PART_MAIN,LV_STATE_DEFAULT,font);
  lv_obj_set_style_local_text_color(o,LV_LABEL_PART_MAIN,LV_STATE_DEFAULT,lv_color_hex(0xe8ede7));
  lv_obj_set_style_local_text_line_space(o,LV_LABEL_PART_MAIN,LV_STATE_DEFAULT,0);
  lv_obj_set_click(o,false); return o;
}
void CompactUi::begin() {
  if(root_) return;
  root_=lv_obj_create(nullptr,nullptr); lv_obj_set_size(root_,240,135);
  lv_obj_set_style_local_bg_color(root_,LV_OBJ_PART_MAIN,LV_STATE_DEFAULT,lv_color_hex(0x090e0d));
  lv_obj_set_style_local_bg_opa(root_,LV_OBJ_PART_MAIN,LV_STATE_DEFAULT,LV_OPA_COVER);
  lv_obj_set_style_local_radius(root_,LV_OBJ_PART_MAIN,LV_STATE_DEFAULT,0);
  lv_obj_set_style_local_border_width(root_,LV_OBJ_PART_MAIN,LV_STATE_DEFAULT,3);
  icon_=label(9,9,19,&lv_font_montserrat_16);
  name_=label(30,6,157,&vibe_jp_16); batteryLabel_=label(192,10,40,&lv_font_montserrat_12);
  body_=label(10,34,220,&vibe_jp_16); lv_obj_set_height(body_,63);
  footer_=label(10,108,190,&vibe_jp_16); counter_=label(199,113,34,&lv_font_montserrat_12);
  choiceLabel_=label(12,41,215,&lv_font_montserrat_16); lv_label_set_align(choiceLabel_,LV_LABEL_ALIGN_CENTER);
  progress_=lv_obj_create(root_,nullptr); lv_obj_set_pos(progress_,12,94); lv_obj_set_size(progress_,1,3);
  lv_obj_set_style_local_border_width(progress_,LV_OBJ_PART_MAIN,LV_STATE_DEFAULT,0);
  lv_obj_set_style_local_bg_color(progress_,LV_OBJ_PART_MAIN,LV_STATE_DEFAULT,lv_color_hex(0x85b998));
  lv_obj_set_style_local_bg_opa(progress_,LV_OBJ_PART_MAIN,LV_STATE_DEFAULT,LV_OPA_COVER);
  lv_obj_set_click(progress_,false); lv_scr_load(root_); paginate(); refresh();
}
void CompactUi::paginate() {
  pages_.clear(); std::string line, page; int lines=0;
  const auto pushLine=[&](){ if(lines) page+='\n'; page+=line; line.clear(); if(++lines==3){pages_.push_back(page);page.clear();lines=0;} };
  for(size_t i=0;i<message_.size();) {
    size_t n=1; auto c=static_cast<unsigned char>(message_[i]);
    if((c&0xe0)==0xc0)n=2;else if((c&0xf0)==0xe0)n=3;else if((c&0xf8)==0xf0)n=4;
    n=std::min(n,message_.size()-i); const auto ch=message_.substr(i,n); i+=n;
    if(ch=="\r")continue;
    if(ch=="\n"){pushLine();continue;}
    const auto candidate=line+ch;
    if(!line.empty() && _lv_txt_get_width(candidate.c_str(),candidate.size(),&vibe_jp_16,0,LV_TXT_FLAG_NONE)>220) pushLine();
    line+=ch;
  }
  if(!line.empty())pushLine();
  if(!page.empty())pages_.push_back(page);
  if(pages_.empty())pages_.push_back("");
  page_=std::min<int>(page_,pages_.size()-1);
}
bool CompactUi::receive(const char* json) {
  if(!json || std::strlen(json)>65536)return false;
  JsonDocument d; if(deserializeJson(d,json))return false;
  const auto type=text(d["type"]);
  if(type=="ack") {
    // Only a matching UI/action acknowledgement may resolve an in-flight answer.
    if(pending_.empty() || text(d["uiId"])!=id_ || text(d["actionId"])!=pending_)return false;
    if(d["ok"]==true){acknowledged_=true;error_.clear();}
    else {pending_.clear();error_=text(d["error"],"送信できません。再確認してください");}
    dirty_=true;refresh();return true;
  }
  if(type!="state")return false;
  const auto ui=d["ui"].as<JsonObjectConst>();
  const auto id=text(ui["id"]), session=text(d["session"]["id"]);
  const auto status=id.empty()?text(d["agent"]["status"],"idle"):text(ui["state"],"waiting");
  const auto message=id.empty()?text(d["agent"]["message"],"操作を待っています"):text(ui["message"]);
  std::vector<Action> actions;
  for(auto a:ui["actions"].as<JsonArrayConst>()) {
    if(actions.size()==2)break;
    auto aid=text(a["id"]); if(!aid.empty())actions.push_back({aid,text(a["label"]),text(a["button"])});
  }
  bool changed=id!=id_ || session!=sessionId_ || status!=status_ || message!=message_ || actions.size()!=actions_.size();
  for(size_t i=0;!changed && i<actions.size();++i)changed=actions[i].id!=actions_[i].id || actions[i].label!=actions_[i].label || actions[i].button!=actions_[i].button;
  if(changed){cancel();++generation_;page_=0;answering_=false;choice_=actions.size()>1?1:0;pending_.clear();error_.clear();acknowledged_=false;outgoing_.clear();}
  id_=id;sessionId_=session;sessionName_=text(d["session"]["name"]);
  std::replace(sessionName_.begin(),sessionName_.end(),'\n',' '); std::replace(sessionName_.begin(),sessionName_.end(),'\r',' ');
  status_=status;message_=message;actions_=actions;
  if(changed)paginate();dirty_=true;refresh();return true;
}
void CompactUi::cancel(){pressed_=0;holdStep_=-1;dirty_=true;}
void CompactUi::hardware(bool powered,bool connected,int battery) {
  if(powered_!=powered || connected_!=connected){cancel();++generation_;answering_=false;page_=0;outgoing_.clear();}
  powered_=powered;connected_=connected;battery_=std::clamp(battery,0,100);dirty_=true;refresh();
}
bool CompactUi::canAnswer()const{return powered_ && connected_ && status_=="waiting" && !id_.empty() && !actions_.empty() && pending_.empty();}
void CompactUi::button(char key,int phase) {
  if(phase<0){cancel();refresh();return;}
  if(!powered_ || !connected_)return;
  if(phase==1){
    if(pressed_){cancel();refresh();return;}
    pressed_=key;pressedAt_=lv_tick_get();pressGeneration_=generation_;dirty_=true;refresh();return;
  }
  if(pressed_!=key || pressGeneration_!=generation_){cancel();return;}
  const auto elapsed=lv_tick_elaps(pressedAt_);cancel();
  if(key=='C'){answering_=false;page_=0;}
  else if(pending_.empty() && answering_ && canAnswer()) {
    if(key=='A')choice_=(choice_+1)%actions_.size();
    else if(key=='B' && elapsed>=900){
      pending_=actions_[choice_].id; JsonDocument d;
      d["type"]="uiAction";d["uiId"]=id_;d["actionId"]=pending_;d["button"]=actions_[choice_].button;
      serializeJson(d,outgoing_);acknowledged_=false;
    }
  } else if(pending_.empty()) {
    if(key=='A'){if(page_+1<(int)pages_.size())++page_;else if(canAnswer())answering_=true;}
    if(key=='B' && page_>0)--page_;
  }
  dirty_=true;refresh();
}
void CompactUi::refresh() {
  if(!root_)return;
  const int step=pressed_=='B' && answering_ && canAnswer()?std::min<uint32_t>(10,lv_tick_elaps(pressedAt_)/90):0;
  if(step!=holdStep_){holdStep_=step;dirty_=true;}
  if(!dirty_)return;dirty_=false;
  lv_obj_set_style_local_border_color(root_,LV_OBJ_PART_MAIN,LV_STATE_DEFAULT,lv_color_hex(connected_?frame(status_):0x4d5552));
  lv_label_set_text(icon_,!connected_?LV_SYMBOL_CLOSE:status_=="waiting"?"?":status_=="running"?LV_SYMBOL_PLAY:status_=="failed"?LV_SYMBOL_WARNING:LV_SYMBOL_PAUSE);
  lv_obj_set_style_local_text_color(icon_,LV_LABEL_PART_MAIN,LV_STATE_DEFAULT,lv_color_hex(connected_?frame(status_)==0x750e23?0xd7969e:status_=="running"?0x63c583:0xa9b5af:0xa9b5af));
  lv_label_set_text(name_,shorten(sessionName_.empty()?stateName(status_):sessionName_,157).c_str());
  lv_label_set_text(batteryLabel_,(std::to_string(battery_)+"%").c_str());
  const bool choose=answering_ && canAnswer();
  lv_obj_set_hidden(choiceLabel_,!choose);lv_obj_set_hidden(progress_,!choose || step==0);
  if(choose){
    const auto left=actions_.empty()?"":actions_[0].label;
    const auto right=actions_.size()>1?actions_[1].label:"";
    const auto choices=(choice_==0?"[ "+left+" ]":left)+"    "+(choice_==1?"[ "+right+" ]":right);
    lv_label_set_text(choiceLabel_,choices.c_str()); lv_obj_set_width(progress_,std::max(1,step*21));
  }
  const std::string body=!connected_?"PCとの接続がありません":!pending_.empty() && answering_?(acknowledged_?"PCが回答を受信しました":"PCへ送信中です"):choose?"":pages_[page_];
  lv_label_set_text(body_,body.c_str());
  const std::string hint=!error_.empty()?error_:!connected_?"接続を確認してください":!pending_.empty()?"C:本文":choose?"A:切替 B:長押し":(page_+1<(int)pages_.size()?"A:次 B:前":canAnswer()?"A:回答 B:前":"A:次 B:前");
  lv_label_set_text(footer_,hint.c_str());
  lv_label_set_text(counter_,choose?"0.9s":(std::to_string(page_+1)+"/"+std::to_string(pages_.size())).c_str());
  lv_obj_set_hidden(root_,!powered_);
}
std::string CompactUi::snapshot()const {
  JsonDocument d;d["state"]=status_;d["uiId"]=id_;d["session"]=sessionName_;d["message"]=message_;
  d["page"]=page_;d["pages"]=pages_.size();d["displayMessage"]=pages_.empty()?"":pages_[page_];
  d["answering"]=answering_;d["choice"]=choice_;d["pending"]=pending_;d["acknowledged"]=acknowledged_;
  d["connected"]=connected_;d["powered"]=powered_;d["holdMs"]=900;d["holdStep"]=holdStep_;d["error"]=error_;
  d["label"]=sessionName_+"。"+stateName(status_)+"。"+(pages_.empty()?message_:pages_[page_]);
  d["frameColor"]=frame(status_);std::string out;serializeJson(d,out);return out;
}
std::string CompactUi::takeAction(){auto out=outgoing_;outgoing_.clear();return out;}
}
