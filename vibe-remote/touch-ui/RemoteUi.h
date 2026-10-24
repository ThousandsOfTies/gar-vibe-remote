#pragma once
#include <ArduinoJson.h>
#include <lvgl/lvgl.h>
#include <string>
#include <vector>

namespace vibe {
// This class is compiled unchanged for the watch and the browser. Platform code
// supplies LVGL display/input drivers and transports the outgoing JSON messages.
class RemoteUi {
 public:
  static constexpr int holdMs = 900;
  void begin();
  bool receive(const char* json, size_t length);
  void hardware(bool powered, bool connected, int battery, bool charging, bool demo = false);
  void reset();
  void refresh();
  void sideButton();
  void scroll(int pixels);
  std::string takeAction();
  std::string snapshot() const;

 private:
  struct Action { std::string id, label, button; };
  struct Binding { RemoteUi* owner; int index; unsigned generation, pressedGeneration; };
  static void event(lv_obj_t* object, lv_event_t event);
  static void sessionEvent(lv_obj_t* object, lv_event_t event);
  void activate(int index, bool held, const char* physicalButton = nullptr);
  void queue(const Action& action, const std::string& button);
  bool enabled() const;
  bool selectorOpen() const;
  void syncSelectorShield();
  bool requiresHold(const Action& action) const;
  void clearState();
  lv_obj_t* label(int x, int y, int width, const lv_font_t* font, uint32_t colour);
  std::string id_, title_, status_ = "idle", stateLabel_, message_, error_, pending_, outgoing_;
  std::string sessionId_, sessionName_, selectorOptions_;
  bool direct_ = false, powered_ = true, connected_ = false, charging_ = false, demo_ = false;
  bool hideStatus_ = false, resetMessageScroll_ = true;
  bool selectorShieldPressed_ = false;
  int battery_ = 82;
  unsigned generation_ = 0;
  bool rebuild_ = true, dirty_ = true;
  std::vector<Action> actions_;
  Binding bindings_[6]{};
  lv_obj_t* buttons_[6]{};
  lv_obj_t *root_ = nullptr, *link_ = nullptr, *batteryLabel_ = nullptr;
  lv_obj_t *titleLabel_ = nullptr, *statusLabel_ = nullptr, *messageLabel_ = nullptr;
  lv_obj_t *page_ = nullptr, *messagePage_ = nullptr;
  lv_obj_t *selector_ = nullptr, *sessionLabel_ = nullptr, *sessionIcon_ = nullptr, *selectorArrow_ = nullptr;
  lv_obj_t* selectorShield_ = nullptr;
};
}
