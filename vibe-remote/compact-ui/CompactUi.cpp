#include "CompactUi.h"
#include <algorithm>
#include <cstring>
#include <set>
LV_FONT_DECLARE(vibe_jp_16);

namespace vibe {
namespace {
std::string text(JsonVariantConst v, const char* fallback="") {
  return v.is<const char*>() ? v.as<const char*>() : fallback;
}
uint32_t frame(const std::string& status) {
  return status=="running" ? 0x238a43 : (status=="waiting" || status=="failed") ? 0x9c263b : 0x4d5552;
}
uint32_t sessionBackground(const std::string& status) {
  // Large, brighter color areas remain distinguishable on the physical TFT.
  // Every name uses black text, including neutral and disconnected states.
  return status=="running" ? 0x79c98e : (status=="waiting" || status=="failed") ? 0xef7f87 : 0xb9c1bd;
}
const char* icon(const std::string& status) {
  return status=="waiting" ? "?" : status=="running" ? LV_SYMBOL_PLAY : status=="failed" ? LV_SYMBOL_WARNING : LV_SYMBOL_PAUSE;
}
const char* stateName(const std::string& status) {
  return status=="running" ? "実行中" : status=="waiting" ? "入力待ち" : status=="done" ? "完了" : status=="failed" ? "エラー" : "待機中";
}
size_t charSize(const std::string& s, size_t i) {
  const auto c=static_cast<unsigned char>(s[i]);
  const size_t n=(c&0xe0)==0xc0 ? 2 : (c&0xf0)==0xe0 ? 3 : (c&0xf8)==0xf0 ? 4 : 1;
  return std::min(n,s.size()-i);
}
std::string shorten(std::string s, int width) {
  const auto fits=[width](const std::string& value) {
    return _lv_txt_get_width(value.c_str(),value.size(),&vibe_jp_16,0,LV_TXT_FLAG_NONE)<=width;
  };
  if(fits(s))return s;
  while(!s.empty()) {
    size_t p=s.size()-1; while(p && (static_cast<unsigned char>(s[p])&0xc0)==0x80)--p;
    s.resize(p); if(fits(s+"..."))return s+"...";
  }
  return "...";
}
std::string wrap(const std::string& message, int width) {
  std::string out,line;
  for(size_t i=0;i<message.size();) {
    const auto ch=message.substr(i,charSize(message,i)); i+=ch.size();
    if(ch=="\r")continue;
    if(ch=="\n") { out+=line+'\n';line.clear();continue; }
    const auto candidate=line+ch;
    if(!line.empty() && _lv_txt_get_width(candidate.c_str(),candidate.size(),&vibe_jp_16,0,LV_TXT_FLAG_NONE)>width) {
      out+=line+'\n'; line.clear();
    }
    line+=ch;
  }
  return out+line;
}
void box(lv_obj_t* o, uint32_t bg, int border, uint32_t edge) {
  lv_obj_set_style_local_bg_color(o,LV_OBJ_PART_MAIN,LV_STATE_DEFAULT,lv_color_hex(bg));
  lv_obj_set_style_local_bg_opa(o,LV_OBJ_PART_MAIN,LV_STATE_DEFAULT,LV_OPA_COVER);
  lv_obj_set_style_local_border_color(o,LV_OBJ_PART_MAIN,LV_STATE_DEFAULT,lv_color_hex(edge));
  lv_obj_set_style_local_border_width(o,LV_OBJ_PART_MAIN,LV_STATE_DEFAULT,border);
  lv_obj_set_style_local_border_opa(o,LV_OBJ_PART_MAIN,LV_STATE_DEFAULT,LV_OPA_COVER);
  lv_obj_set_style_local_border_side(o,LV_OBJ_PART_MAIN,LV_STATE_DEFAULT,LV_BORDER_SIDE_FULL);
  lv_obj_set_style_local_radius(o,LV_OBJ_PART_MAIN,LV_STATE_DEFAULT,0);
  lv_obj_set_style_local_pad_all(o,LV_OBJ_PART_MAIN,LV_STATE_DEFAULT,0);
  lv_obj_set_click(o,false);
}
}

lv_obj_t* CompactUi::label(lv_obj_t* parent,int x,int y,int width,const lv_font_t* font) {
  auto* o=lv_label_create(parent,nullptr);
  lv_label_set_long_mode(o,LV_LABEL_LONG_CROP);lv_obj_set_width(o,width);lv_obj_set_pos(o,x,y);
  lv_obj_set_style_local_text_font(o,LV_LABEL_PART_MAIN,LV_STATE_DEFAULT,font);
  lv_obj_set_style_local_text_color(o,LV_LABEL_PART_MAIN,LV_STATE_DEFAULT,lv_color_hex(0xe8ede7));
  lv_obj_set_style_local_text_line_space(o,LV_LABEL_PART_MAIN,LV_STATE_DEFAULT,0);
  lv_obj_set_style_local_text_letter_space(o,LV_LABEL_PART_MAIN,LV_STATE_DEFAULT,0);
  lv_obj_set_click(o,false);return o;
}
void CompactUi::begin() {
  if(root_)return;
  root_=lv_obj_create(nullptr,nullptr);lv_obj_set_size(root_,135,240);box(root_,0x090e0d,0,0);
  icon_=label(root_,9,10,19,&lv_font_montserrat_16);
  counter_=label(root_,37,12,51,&lv_font_montserrat_12);
  batteryLabel_=label(root_,94,12,34,&lv_font_montserrat_12);
  nameBand_=lv_obj_create(root_,nullptr);lv_obj_set_pos(nameBand_,9,29);lv_obj_set_size(nameBand_,textWidth,26);
  box(nameBand_,sessionBackground("idle"),0,0);
  name_=label(nameBand_,4,3,textWidth-8,&vibe_jp_16);
  lv_obj_set_style_local_text_color(name_,LV_LABEL_PART_MAIN,LV_STATE_DEFAULT,lv_color_hex(0x000000));
  viewport_=lv_obj_create(root_,nullptr);lv_obj_set_pos(viewport_,9,textTop);lv_obj_set_size(viewport_,textWidth,fullHeight);box(viewport_,0x090e0d,0,0);
  body_=label(viewport_,0,0,textWidth,&vibe_jp_16);
  footer_=label(root_,9,212,textWidth,&vibe_jp_16);
  for(int i=0;i<2;++i) {
    optionRows_[i]=lv_obj_create(root_,nullptr);lv_obj_set_pos(optionRows_[i],9,156+i*28);lv_obj_set_size(optionRows_[i],textWidth,25);
    optionLabels_[i]=label(optionRows_[i],5,2,textWidth-10,&vibe_jp_16);
  }
  for(int i=0;i<4;++i) {
    listRows_[i]=lv_obj_create(root_,nullptr);lv_obj_set_pos(listRows_[i],9,60+i*37);lv_obj_set_size(listRows_[i],textWidth,32);
    listIcons_[i]=label(listRows_[i],5,9,17,&lv_font_montserrat_16);
    listLabels_[i]=label(listRows_[i],25,6,86,&vibe_jp_16);
    lv_obj_set_style_local_text_color(listLabels_[i],LV_LABEL_PART_MAIN,LV_STATE_DEFAULT,lv_color_hex(0x000000));
  }
  // Keep the state frame inside the physical LCD bezel, independent of the
  // active theme's screen border. This last, passive child stays above content.
  frame_=lv_obj_create(root_,nullptr);lv_obj_set_pos(frame_,2,2);lv_obj_set_size(frame_,131,236);
  box(frame_,0x090e0d,4,0x4d5552);
  lv_obj_set_style_local_bg_opa(frame_,LV_OBJ_PART_MAIN,LV_STATE_DEFAULT,LV_OPA_TRANSP);
  lastTick_=lv_tick_get();lv_scr_load(root_);refresh();
}
CompactUi::Session* CompactUi::current() { return sessions_.empty() ? nullptr : &sessions_[selected_]; }
const CompactUi::Session* CompactUi::current() const { return sessions_.empty() ? nullptr : &sessions_[selected_]; }
int CompactUi::find(const std::string& id) const {
  for(size_t i=0;i<sessions_.size();++i)if(sessions_[i].id==id)return i;
  return -1;
}
bool CompactUi::hasOptions(const Session& s) const { return s.status=="waiting" && !s.uiId.empty() && !s.actions.empty() && s.pending.empty(); }
bool CompactUi::canAnswer(const Session& s) const { return powered_ && connected_ && hasOptions(s); }
int CompactUi::waitingCount() const {
  return std::count_if(sessions_.begin(),sessions_.end(),[this](const Session& s){return hasOptions(s);});
}
bool CompactUi::parseSession(JsonObjectConst object,Session& s) const {
  if(object.isNull())return false;
  s.id=text(object["session"]["id"]);s.name=text(object["session"]["name"]);
  if(s.id.empty() || s.id.size()>256 || s.name.size()>512)return false;
  std::replace(s.name.begin(),s.name.end(),'\n',' ');std::replace(s.name.begin(),s.name.end(),'\r',' ');
  const auto ui=object["ui"].as<JsonObjectConst>();s.uiId=text(ui["id"]);
  s.status=s.uiId.empty()?text(object["agent"]["status"],"idle"):text(ui["state"],"waiting");
  s.message=s.uiId.empty()?text(object["agent"]["message"],"操作を待っています"):text(ui["message"]);
  if(s.uiId.size()>256 || s.message.size()>8192)return false;
  std::set<std::string> ids;
  if(!s.uiId.empty())for(auto a:ui["actions"].as<JsonArrayConst>()) {
    Action action{text(a["id"]),text(a["label"]),text(a["button"])};
    if(action.id.empty() || action.id.size()>256 || action.label.empty() || action.label.size()>128 ||
       !ids.insert(action.id).second || s.actions.size()==maxActions)return false;
    s.actions.push_back(action);
  }
  s.choice=0;
  for(size_t i=0;i<s.actions.size();++i)if(s.actions[i].id=="no")s.choice=i;
  s.wrapped=wrap(s.message,textWidth);
  s.contentHeight=(1+std::count(s.wrapped.begin(),s.wrapped.end(),'\n'))*vibe_jp_16.line_height;
  return s.contentHeight<=30000; // Keep every LVGL 7 coordinate inside int16_t.
}
bool CompactUi::merge(Session& next,const Session& old) {
  bool same=next.uiId==old.uiId && next.status==old.status && next.message==old.message && next.actions.size()==old.actions.size();
  for(size_t i=0;same && i<next.actions.size();++i)
    same=next.actions[i].id==old.actions[i].id && next.actions[i].label==old.actions[i].label && next.actions[i].button==old.actions[i].button;
  if(same) {
    const auto name=next.name;next=old;next.name=name;
  }
  return same;
}
void CompactUi::cancel() { pressed_=0; }
void CompactUi::invalidate() { cancel();++generation_; }
void CompactUi::discardOutgoing(const std::string& sessionId) {
  outgoing_.erase(std::remove_if(outgoing_.begin(),outgoing_.end(),[&](const Outgoing& item){return item.sessionId==sessionId;}),outgoing_.end());
}
bool CompactUi::receive(const char* json) {
  if(!json || std::strlen(json)>65536)return false;
  JsonDocument d;if(deserializeJson(d,json))return false;
  refresh();const auto type=text(d["type"]);
  if(type=="ack") {
    const int index=find(text(d["sessionId"]));if(index<0)return false;
    auto& s=sessions_[index];
    if(s.pending.empty() || text(d["uiId"])!=s.uiId || text(d["actionId"])!=s.pending || !d["ok"].is<bool>())return false;
    if(d["ok"].as<bool>()) { s.acknowledged=true;s.error.clear(); }
    else { s.pending.clear();s.error="送信エラー\n選び直してください"; }
    dirty_=true;refresh();return true;
  }
  if(type=="sessions") {
    const auto array=d["sessions"].as<JsonArrayConst>();
    if(array.isNull() || array.size()>maxSessions)return false;
    std::vector<Session> next;std::set<std::string> ids;
    // Validate the entire authoritative snapshot before modifying live state.
    for(auto item:array) {
      Session s;if(!parseSession(item.as<JsonObjectConst>(),s) || !ids.insert(s.id).second)return false;
      next.push_back(std::move(s));
    }
    const auto selectedId=current()?current()->id:"";
    const auto focusId=listOpen_ && !sessions_.empty()?sessions_[listFocus_].id:selectedId;
    bool changedSelected=false,changedList=next.size()!=sessions_.size();
    for(size_t i=0;i<next.size();++i) {
      const int old=find(next[i].id);
      if(old>=0 && !merge(next[i],sessions_[old])) {
        discardOutgoing(next[i].id);if(next[i].id==selectedId)changedSelected=true;
      }
      if(i>=sessions_.size() || sessions_[i].id!=next[i].id)changedList=true;
    }
    for(const auto& old:sessions_)if(!ids.count(old.id))discardOutgoing(old.id);
    sessions_=std::move(next);
    selected_=find(selectedId);if(selected_<0){selected_=0;changedSelected=true;}
    listFocus_=find(focusId);if(listFocus_<0)listFocus_=selected_;
    if(changedSelected || (listOpen_ && changedList))invalidate();
    dirty_=true;refresh();return true;
  }
  if(type!="state")return false;
  Session next;if(!parseSession(d.as<JsonObjectConst>(),next))return false;
  const int old=find(next.id);
  if(old<0) {
    if(sessions_.size()==maxSessions)return false;
    if(listOpen_ || sessions_.empty())invalidate();
    sessions_.push_back(std::move(next));
  } else {
    if(!merge(next,sessions_[old])) { discardOutgoing(next.id);if(old==selected_)invalidate(); }
    sessions_[old]=std::move(next);
  }
  dirty_=true;refresh();return true;
}
void CompactUi::hardware(bool powered,bool connected,int battery) {
  refresh();
  if(powered_!=powered || connected_!=connected) { invalidate();outgoing_.clear();listOpen_=false; }
  powered_=powered;connected_=connected;battery_=std::clamp(battery,0,100);dirty_=true;refresh();
}
void CompactUi::button(char key,int phase) {
  refresh();
  if(phase<0) { cancel();return; }
  if(!powered_ || (key!='A' && key!='B' && key!='C'))return;
  if(phase==1) {
    if(pressed_){cancel();return;}
    pressed_=key;pressedAt_=lv_tick_get();pressGeneration_=generation_;return;
  }
  if(pressed_!=key || pressGeneration_!=generation_) { cancel();return; }
  const auto duration=lv_tick_elaps(pressedAt_);cancel();
  // C is also the physical power switch; a long power gesture is not a focus step.
  if(key=='C' && duration>=1000)return;
  if(key=='B') {
    listOpen_=!listOpen_;listFocus_=selected_;invalidate();
  } else if(listOpen_) {
    if(key=='C' && !sessions_.empty())listFocus_=(listFocus_+1)%sessions_.size();
    if(key=='A' && !sessions_.empty()) { selected_=listFocus_;listOpen_=false;invalidate(); }
  } else if(auto* s=current(); s && canAnswer(*s)) {
    if(key=='C') { s->choice=(s->choice+1)%s->actions.size();s->error.clear(); }
    if(key=='A') {
      const auto& action=s->actions[s->choice];s->pending=action.id;s->acknowledged=false;s->error.clear();
      JsonDocument d;d["type"]="uiAction";d["sessionId"]=s->id;d["uiId"]=s->uiId;d["actionId"]=action.id;d["button"]=action.button;
      std::string value;serializeJson(d,value);outgoing_.push_back({s->id,std::move(value)});
    }
  }
  dirty_=true;refresh();
}
void CompactUi::advanceScroll(uint32_t elapsed) {
  auto* s=current();
  if(!s || !powered_ || !connected_ || listOpen_ || pressed_ || !s->pending.empty() || !s->error.empty())return;
  const int height=hasOptions(*s)?questionHeight:fullHeight;
  const int maximum=std::max(0,s->contentHeight-height);
  if(!maximum)return;
  const uint64_t travel=(static_cast<uint64_t>(maximum)*1000+scrollSpeed-1)/scrollSpeed;
  const uint64_t cycle=1800+travel+2800;
  s->scrollTime=(s->scrollTime+elapsed)%cycle;
  const int position=s->scrollTime<=1800?0:std::min<uint64_t>(maximum,(s->scrollTime-1800)*scrollSpeed/1000);
  if(position!=s->scrollPixels) { s->scrollPixels=position;dirty_=true; }
}
void CompactUi::refresh() {
  if(!root_)return;
  const auto now=lv_tick_get();const auto elapsed=now-lastTick_;lastTick_=now;
  advanceScroll(elapsed);
  if(!dirty_)return;dirty_=false;render();
}
void CompactUi::render() {
  const auto* s=current();const auto status=s?s->status:"idle";
  lv_obj_set_hidden(root_,!powered_);
  lv_obj_set_style_local_border_color(frame_,LV_OBJ_PART_MAIN,LV_STATE_DEFAULT,lv_color_hex(connected_?frame(status):0x4d5552));
  lv_label_set_text(icon_,connected_?icon(status):LV_SYMBOL_CLOSE);
  lv_obj_set_style_local_text_color(icon_,LV_LABEL_PART_MAIN,LV_STATE_DEFAULT,lv_color_hex(!connected_?0xa9b5af:status=="running"?0x63c583:(status=="waiting" || status=="failed")?0xd7969e:0xa9b5af));
  lv_label_set_text(batteryLabel_,(std::to_string(battery_)+"%").c_str());
  lv_label_set_text(counter_,listOpen_?(sessions_.empty()?"0/0":(std::to_string(listFocus_+1)+"/"+std::to_string(sessions_.size())).c_str()):("? "+std::to_string(waitingCount())).c_str());
  lv_obj_set_style_local_bg_color(nameBand_,LV_OBJ_PART_MAIN,LV_STATE_DEFAULT,lv_color_hex(sessionBackground(!connected_ || listOpen_?"idle":status)));
  lv_label_set_text(name_,listOpen_?"セッション":shorten(s?(s->name.empty()?stateName(status):s->name):"待機中",textWidth-8).c_str());
  lv_obj_set_hidden(viewport_,listOpen_ && !sessions_.empty());
  const bool options=!listOpen_ && s && connected_ && hasOptions(*s);
  for(int i=0;i<2;++i) {
    const int first=s?(s->choice/2)*2:0;const int index=first+i;
    const bool show=options && index<(int)s->actions.size();
    lv_obj_set_hidden(optionRows_[i],!show);
    if(show) {
      const bool focus=index==s->choice;
      box(optionRows_[i],focus?0x26372f:0x090e0d,1,focus?0xa1b5a8:0x38473d);
      lv_label_set_text(optionLabels_[i],shorten((focus?"> ":"  ")+s->actions[index].label,textWidth-10).c_str());
    }
  }
  for(int i=0;i<4;++i) {
    const int index=(listFocus_/4)*4+i;const bool show=listOpen_ && index<(int)sessions_.size();
    lv_obj_set_hidden(listRows_[i],!show);
    if(show) {
      const auto& entry=sessions_[index];const bool focus=index==listFocus_;
      box(listRows_[i],sessionBackground(connected_?entry.status:"idle"),focus?3:1,0x000000);
      lv_label_set_text(listIcons_[i],icon(entry.status));
      lv_obj_set_style_local_text_color(listIcons_[i],LV_LABEL_PART_MAIN,LV_STATE_DEFAULT,lv_color_hex(0x000000));
      lv_label_set_text(listLabels_[i],shorten(entry.name.empty()?stateName(entry.status):entry.name,86).c_str());
    }
  }
  if(listOpen_ && sessions_.empty()) {
    lv_obj_set_height(viewport_,fullHeight);lv_label_set_text(body_,"セッションは\nありません");
    lv_obj_set_height(body_,fullHeight);lv_obj_set_y(body_,0);lv_label_set_text(footer_,"B:戻る");
  } else if(!listOpen_) {
    const int height=options?questionHeight:fullHeight;lv_obj_set_height(viewport_,height);
    std::string body;int y=0;
    if(!s)body="セッションを\n待っています";
    else if(!connected_)body="PCとの接続が\nありません";
    else if(!s->pending.empty())body=s->acknowledged?"PCが回答を\n受信しました":"PCへ送信中です";
    else if(!s->error.empty())body=wrap(s->error,textWidth);
    else { body=s->wrapped;y=-s->scrollPixels; }
    lv_label_set_text(body_,body.c_str());
    lv_obj_set_height(body_,std::max(height,(1+(int)std::count(body.begin(),body.end(),'\n'))*vibe_jp_16.line_height));
    lv_obj_set_y(body_,y);
    lv_label_set_text(footer_,options?"C:選択 A:決定":"B:一覧");
  } else lv_label_set_text(footer_,"C:次 A:決定");
}
std::string CompactUi::snapshot() const {
  JsonDocument d;const auto* s=current();
  d["state"]=s?s->status:"idle";d["uiId"]=s?s->uiId:"";d["sessionId"]=s?s->id:"";d["session"]=s?s->name:"";
  d["message"]=s?s->message:"";d["displayMessage"]=s?s->wrapped:"";
  d["choice"]=s?s->choice:0;d["pending"]=s?s->pending:"";d["acknowledged"]=s && s->acknowledged;d["error"]=s?s->error:"";
  d["connected"]=connected_;d["powered"]=powered_;d["listOpen"]=listOpen_;d["listFocus"]=listFocus_;
  d["waitingCount"]=waitingCount();d["scrollPixels"]=s?s->scrollPixels:0;d["scrollSpeed"]=scrollSpeed;
  d["bodyHeight"]=s && connected_ && hasOptions(*s)?questionHeight:fullHeight;d["contentHeight"]=s?s->contentHeight:0;
  d["frameColor"]=connected_?frame(s?s->status:"idle"):0x4d5552;
  d["frameInset"]=2;d["frameWidth"]=4;
  d["sessionNameBackground"]=sessionBackground(!connected_ || listOpen_?"idle":s?s->status:"idle");
  d["sessionNameTextColor"]=0x000000;
  const bool answerable=s && !listOpen_ && canAnswer(*s);d["answerable"]=answerable;
  auto actions=d["actions"].to<JsonArray>();
  if(s)for(size_t i=0;i<s->actions.size();++i) {
    auto a=actions.add<JsonObject>();a["id"]=s->actions[i].id;a["label"]=s->actions[i].label;a["focused"]=answerable && (int)i==s->choice;
  }
  auto sessions=d["sessions"].to<JsonArray>();
  for(size_t i=0;i<sessions_.size();++i) {
    auto entry=sessions.add<JsonObject>();entry["id"]=sessions_[i].id;entry["name"]=sessions_[i].name;entry["state"]=sessions_[i].status;
    entry["selected"]=(int)i==selected_;entry["focused"]=listOpen_ && (int)i==listFocus_;entry["pending"]=sessions_[i].pending;
  }
  if(listOpen_)d["label"]=sessions_.empty()?"セッションはありません":"セッション一覧。"+sessions_[listFocus_].name+"。Cで次、Aで決定。";
  else d["label"]=s?s->name+"。"+stateName(s->status)+"。"+s->message:"セッションを待っています";
  std::string out;serializeJson(d,out);return out;
}
std::string CompactUi::takeAction() {
  if(outgoing_.empty())return "";
  auto value=outgoing_.front().json;outgoing_.pop_front();return value;
}
}
