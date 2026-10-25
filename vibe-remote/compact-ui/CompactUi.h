#pragma once
#include <lvgl.h>
#include <ArduinoJson.h>
#include <deque>
#include <string>
#include <vector>

namespace vibe {
// Platform-independent portrait UI. The board supplies JSON, button edges,
// connection facts and an LVGL display/clock. No transport or GPIO dependency.
class CompactUi {
 public:
  void begin();
  bool receive(const char* json);
  void hardware(bool powered, bool connected, int battery);
  void button(char key, int phase); // 1 down, 0 up, -1 cancel
  void refresh();
  std::string snapshot() const;
  std::string takeAction();
 private:
  struct Action { std::string id, label, button; };
  struct Session {
    std::string id, name, uiId, status="idle", message, wrapped, pending, error;
    std::vector<Action> actions;
    int choice=0, contentHeight=21, scrollPixels=0;
    uint64_t scrollTime=0;
    bool acknowledged=false;
  };
  struct Outgoing { std::string sessionId, json; };
  static constexpr int maxSessions=16, maxActions=8, scrollSpeed=9;
  static constexpr int textWidth=117, textTop=60, questionHeight=90, fullHeight=148;
  lv_obj_t *root_=nullptr, *frame_=nullptr, *icon_=nullptr, *nameBand_=nullptr, *name_=nullptr, *batteryLabel_=nullptr,
    *viewport_=nullptr, *body_=nullptr, *footer_=nullptr, *counter_=nullptr;
  lv_obj_t* optionRows_[2]{};
  lv_obj_t* optionLabels_[2]{};
  lv_obj_t* listRows_[4]{};
  lv_obj_t* listIcons_[4]{};
  lv_obj_t* listLabels_[4]{};
  std::vector<Session> sessions_;
  std::deque<Outgoing> outgoing_;
  bool powered_=true, connected_=true, listOpen_=false, dirty_=true;
  int battery_=82, selected_=0, listFocus_=0;
  unsigned generation_=0, pressGeneration_=0;
  uint32_t lastTick_=0, pressedAt_=0;
  char pressed_=0;
  Session* current();
  const Session* current() const;
  int find(const std::string& id) const;
  int waitingCount() const;
  bool parseSession(JsonObjectConst json, Session& session) const;
  bool merge(Session& incoming, const Session& previous);
  bool canAnswer(const Session& session) const;
  bool hasOptions(const Session& session) const;
  void cancel();
  void invalidate();
  void discardOutgoing(const std::string& sessionId);
  void advanceScroll(uint32_t elapsed);
  void render();
  lv_obj_t* label(lv_obj_t* parent, int x, int y, int width, const lv_font_t* font);
};
}
