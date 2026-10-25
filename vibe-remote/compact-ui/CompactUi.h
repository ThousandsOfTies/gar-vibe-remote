#pragma once
#include <lvgl.h>
#include <ArduinoJson.h>
#include <string>
#include <vector>

namespace vibe {
// No browser or board dependencies. The board supplies JSON, connection facts,
// physical button edges and the normal LVGL clock/display driver.
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
  lv_obj_t *root_=nullptr, *icon_=nullptr, *name_=nullptr, *batteryLabel_=nullptr,
    *body_=nullptr, *footer_=nullptr, *counter_=nullptr, *choiceLabel_=nullptr, *progress_=nullptr;
  bool powered_=true, connected_=true, answering_=false, dirty_=true, acknowledged_=false;
  int battery_=82, page_=0, choice_=1, holdStep_=-1;
  unsigned generation_=0, pressGeneration_=0;
  uint32_t pressedAt_=0;
  char pressed_=0;
  std::string id_, sessionId_, sessionName_, status_="idle", message_="操作を待っています", pending_, error_, outgoing_;
  std::vector<std::string> pages_;
  std::vector<Action> actions_;
  void paginate();
  void cancel();
  bool canAnswer() const;
  lv_obj_t* label(int x,int y,int w,const lv_font_t* font);
};
}
