#include "RemoteUi.h"
#include <algorithm>
#include <cstring>

LV_FONT_DECLARE(vibe_jp_16);
LV_FONT_DECLARE(vibe_status_24);

namespace vibe {
namespace {
std::string text(JsonVariantConst value, const char* fallback = "") {
  return value.is<const char*>() ? value.as<const char*>() : fallback;
}
bool terminal(const std::string& status) { return status == "done" || status == "failed"; }
const char* statusName(const std::string& status) {
  if (status == "waiting") return "確認待ち";
  if (status == "running") return "実行中";
  if (status == "done") return "完了";
  if (status == "failed") return "エラー";
  return "待機中";
}
uint32_t accent(const std::string& status) {
  if (status == "waiting" || status == "failed") return 0x945b58;
  if (status == "running") return 0x5d976c;
  return 0x626b65;
}
uint32_t frameColour(const std::string& status) {
  if (status == "waiting" || status == "failed") return 0x63000f;
  if (status == "running") return 0x238a43;
  return 0x4d4d4d;
}
const char* statusIcon(const std::string& status) {
  if (status == "waiting") return "?";
  if (status == "failed") return LV_SYMBOL_WARNING;
  return status == "running" ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE;
}
std::string optionName(std::string name) {
  if (_lv_txt_get_width(name.c_str(), name.size(), &vibe_jp_16, 0, LV_TXT_FLAG_NONE) <= 190) return name;
  while (!name.empty()) {
    size_t start = name.size() - 1;
    while (start && (static_cast<unsigned char>(name[start]) & 0xc0) == 0x80) --start;
    name.resize(start);
    const auto shortened = name + "...";
    if (_lv_txt_get_width(shortened.c_str(), shortened.size(), &vibe_jp_16, 0, LV_TXT_FLAG_NONE) <= 190) return shortened;
  }
  return "...";
}
void background(lv_obj_t* obj, uint32_t colour) {
  lv_obj_set_style_local_bg_color(obj, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colour));
  lv_obj_set_style_local_bg_opa(obj, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
}
}

lv_obj_t* RemoteUi::label(int x, int y, int width, const lv_font_t* font, uint32_t colour) {
  auto* obj = lv_label_create(root_, nullptr);
  lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
  lv_obj_set_width(obj, width);
  lv_obj_set_height(obj, font->line_height);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_style_local_text_font(obj, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, font);
  lv_obj_set_style_local_text_color(obj, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colour));
  return obj;
}

void RemoteUi::begin() {
  if (root_) return;
  root_ = lv_obj_create(nullptr, nullptr);
  lv_obj_clean_style_list(root_, LV_OBJ_PART_MAIN);
  background(root_, 0x090e0d);
  lv_obj_set_size(root_, 240, 240);
  lv_obj_set_style_local_border_width(root_, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 4);
  lv_obj_set_style_local_border_opa(root_, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
  lv_obj_set_style_local_radius(root_, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);
  link_ = label(14, 10, 160, &lv_font_montserrat_12, 0x98c48c);
  batteryLabel_ = label(184, 10, 44, &lv_font_montserrat_12, 0xafc0aa);
  auto* divider = lv_obj_create(root_, nullptr);
  lv_obj_clean_style_list(divider, LV_OBJ_PART_MAIN);
  lv_obj_set_pos(divider, 12, 31);
  lv_obj_set_size(divider, 216, 1);
  background(divider, 0x2c392f);
  titleLabel_ = label(14, 39, 212, &vibe_jp_16, 0x8d9f87);
  statusLabel_ = label(14, 62, 212, &vibe_status_24, 0xedc27b);
  selector_ = lv_dropdown_create(root_, nullptr);
  lv_obj_set_user_data(selector_, this);
  lv_obj_set_event_cb(selector_, sessionEvent);
  lv_obj_set_pos(selector_, 12, 34);
  lv_obj_set_size(selector_, 216, 44);
  lv_dropdown_set_show_selected(selector_, false);
  lv_dropdown_set_text(selector_, "");
  lv_dropdown_set_symbol(selector_, nullptr);
  lv_dropdown_set_max_height(selector_, 126);
  lv_obj_clean_style_list(selector_, LV_DROPDOWN_PART_MAIN);
  background(selector_, 0x090e0d);
  lv_obj_set_style_local_radius(selector_, LV_DROPDOWN_PART_MAIN, LV_STATE_DEFAULT, 6);
  lv_obj_set_style_local_border_width(selector_, LV_DROPDOWN_PART_MAIN, LV_STATE_DEFAULT, 1);
  lv_obj_set_style_local_border_opa(selector_, LV_DROPDOWN_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
  lv_obj_set_style_local_text_font(selector_, LV_DROPDOWN_PART_MAIN, LV_STATE_DEFAULT, &vibe_jp_16);
  // LVGL 7 derives dropdown height from font + padding on every style refresh.
  lv_obj_set_style_local_pad_top(selector_, LV_DROPDOWN_PART_MAIN, LV_STATE_DEFAULT, 11);
  lv_obj_set_style_local_pad_bottom(selector_, LV_DROPDOWN_PART_MAIN, LV_STATE_DEFAULT, 12);
  for (const auto part : {LV_DROPDOWN_PART_LIST, LV_DROPDOWN_PART_SELECTED}) {
    lv_obj_set_style_local_text_font(selector_, part, LV_STATE_DEFAULT, &vibe_jp_16);
    lv_obj_set_style_local_text_color(selector_, part, LV_STATE_DEFAULT, lv_color_hex(0xecf1ec));
    lv_obj_set_style_local_bg_color(selector_, part, LV_STATE_DEFAULT, lv_color_hex(0x1b251f));
    lv_obj_set_style_local_bg_opa(selector_, part, LV_STATE_DEFAULT, LV_OPA_COVER);
    lv_obj_set_style_local_border_color(selector_, part, LV_STATE_DEFAULT, lv_color_hex(0x626b65));
    lv_obj_set_style_local_pad_top(selector_, part, LV_STATE_DEFAULT, 10);
    lv_obj_set_style_local_pad_bottom(selector_, part, LV_STATE_DEFAULT, 10);
  }
  lv_obj_set_style_local_text_line_space(selector_, LV_DROPDOWN_PART_LIST, LV_STATE_DEFAULT, 10);
  sessionIcon_ = label(22, 47, 18, &lv_font_montserrat_16, 0x945b58);
  sessionLabel_ = label(47, 47, 145, &vibe_jp_16, 0xecf1ec);
  selectorArrow_ = label(202, 49, 16, &lv_font_montserrat_12, 0xa5afa8);
  lv_label_set_text(selectorArrow_, LV_SYMBOL_DOWN);
  lv_obj_set_click(sessionIcon_, false);
  lv_obj_set_click(sessionLabel_, false);
  lv_obj_set_click(selectorArrow_, false);
  messagePage_ = lv_page_create(root_, nullptr);
  lv_obj_clean_style_list(messagePage_, LV_PAGE_PART_BG);
  lv_obj_clean_style_list(messagePage_, LV_PAGE_PART_SCROLLABLE);
  lv_obj_set_style_local_pad_all(messagePage_, LV_PAGE_PART_BG, LV_STATE_DEFAULT, 0);
  lv_obj_set_style_local_pad_all(messagePage_, LV_PAGE_PART_SCROLLABLE, LV_STATE_DEFAULT, 0);
  lv_page_set_scrollable_fit2(messagePage_, LV_FIT_PARENT, LV_FIT_TIGHT);
  lv_obj_set_drag_dir(lv_page_get_scrollable(messagePage_), LV_DRAG_DIR_VER);
  lv_page_set_anim_time(messagePage_, 0);
  lv_page_set_scrollbar_mode(messagePage_, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_style_local_bg_color(messagePage_, LV_PAGE_PART_SCROLLBAR, LV_STATE_DEFAULT, lv_color_hex(0x668354));
  messageLabel_ = lv_label_create(messagePage_, nullptr);
  lv_label_set_long_mode(messageLabel_, LV_LABEL_LONG_BREAK);
  lv_obj_set_width(messageLabel_, 200);  // Leave a gutter for the scroll bar.
  lv_obj_set_pos(messageLabel_, 2, 0);
  lv_obj_set_style_local_text_font(messageLabel_, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &vibe_jp_16);
  lv_obj_set_style_local_text_color(messageLabel_, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xecf1ec));
  lv_obj_set_click(messageLabel_, true);
  lv_page_glue_obj(messageLabel_, true);
  page_ = lv_page_create(root_, nullptr);
  lv_obj_set_pos(page_, 12, 119);
  lv_obj_set_size(page_, 216, 84);
  lv_obj_clean_style_list(page_, LV_PAGE_PART_BG);
  lv_obj_clean_style_list(page_, LV_PAGE_PART_SCROLLABLE);
  lv_obj_set_style_local_pad_all(page_, LV_PAGE_PART_BG, LV_STATE_DEFAULT, 0);
  lv_obj_set_style_local_pad_all(page_, LV_PAGE_PART_SCROLLABLE, LV_STATE_DEFAULT, 0);
  lv_page_set_scrl_layout(page_, LV_LAYOUT_OFF);
  lv_page_set_scrollable_fit2(page_, LV_FIT_PARENT, LV_FIT_TIGHT);
  lv_obj_set_drag_dir(lv_page_get_scrollable(page_), LV_DRAG_DIR_VER);
  lv_obj_set_style_local_pad_inner(page_, LV_PAGE_PART_SCROLLABLE, LV_STATE_DEFAULT, 3);
  lv_page_set_anim_time(page_, 0);
  lv_page_set_scrollbar_mode(page_, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_style_local_bg_color(page_, LV_PAGE_PART_SCROLLBAR, LV_STATE_DEFAULT, lv_color_hex(0x668354));
  selectorShield_ = lv_obj_create(root_, nullptr);
  lv_obj_clean_style_list(selectorShield_, LV_OBJ_PART_MAIN);
  lv_obj_set_pos(selectorShield_, 0, 0);
  lv_obj_set_size(selectorShield_, 240, 240);
  lv_obj_set_click(selectorShield_, true);
  lv_obj_set_user_data(selectorShield_, this);
  lv_obj_set_event_cb(selectorShield_, sessionEvent);
  lv_obj_set_hidden(selectorShield_, true);
  lv_scr_load(root_);
  for (auto* input = lv_indev_get_next(nullptr); input; input = lv_indev_get_next(input))
    input->driver.long_press_time = holdMs;
  refresh();
}

void RemoteUi::clearState() {
  id_.clear(); title_.clear(); stateLabel_.clear(); message_.clear(); error_.clear(); pending_.clear(); outgoing_.clear();
  sessionId_.clear(); sessionName_.clear(); selectorOptions_.clear();
  selectorShieldPressed_ = false;
  if (selector_) lv_dropdown_close(selector_);
  status_ = "idle"; direct_ = false; actions_.clear();
  hideStatus_ = false; resetMessageScroll_ = true;
  ++generation_; rebuild_ = true; dirty_ = true;
  lv_indev_reset(nullptr, nullptr);
}
void RemoteUi::reset() { clearState(); refresh(); }

void RemoteUi::hardware(bool powered, bool connected, int battery, bool charging, bool demo) {
  battery = std::max(0, std::min(100, battery));
  if (powered == powered_ && connected == connected_ && battery == battery_ && charging == charging_ && demo == demo_) return;
  if ((!powered && powered_) || (!connected && connected_)) clearState();
  if (connected != connected_ || powered != powered_) rebuild_ = true;
  powered_ = powered; connected_ = connected; battery_ = battery;
  charging_ = charging; demo_ = demo; dirty_ = true;
  refresh();
}

bool RemoteUi::receive(const char* json, size_t length) {
  if (length > 65536) return false;
  JsonDocument doc;
  if (deserializeJson(doc, json, length)) return false;
  const auto type = text(doc["type"]);
  if (type == "ack") {
    if (doc["ok"].is<bool>() && !doc["ok"].as<bool>()) {
      pending_.clear(); error_ = text(doc["error"], "送信が拒否されました"); rebuild_ = true; dirty_ = true;
    }
    refresh(); return true;
  }
  if (type != "state") return false;
  const auto nextSession = text(doc["session"]["id"]);
  auto nextSessionName = text(doc["session"]["name"]);
  // One option per session; never let an incoming title create extra list rows.
  std::replace(nextSessionName.begin(), nextSessionName.end(), '\n', ' ');
  std::replace(nextSessionName.begin(), nextSessionName.end(), '\r', ' ');
  const auto ui = doc["ui"].as<JsonObjectConst>();
  const auto nextId = text(ui["id"]);
  std::vector<Action> next;
  if (!nextId.empty()) {
    for (auto value : ui["actions"].as<JsonArrayConst>()) {
      Action action{text(value["id"]), text(value["label"]), text(value["button"])};
      if (action.id.empty() || action.label.empty()) continue;
      if (action.button.empty()) action.button = next.empty() ? "A" : next.size() == 1 ? "B" : "P";
      next.push_back(action);
      if (next.size() == 6) break;
    }
  }
  const bool direct = text(ui["mode"]) == "direct";
  bool changed = nextId != id_ || nextSession != sessionId_ || direct != direct_ || next.size() != actions_.size();
  for (size_t i = 0; !changed && i < next.size(); ++i)
    changed = next[i].id != actions_[i].id || next[i].label != actions_[i].label || next[i].button != actions_[i].button;
  const auto nextStatus = nextId.empty() ? text(doc["agent"]["status"], "idle") : text(ui["state"], "waiting");
  if (nextId != id_ || terminal(nextStatus) || nextId.empty()) pending_.clear();
  if (changed) {
    lv_dropdown_close(selector_);
    ++generation_; rebuild_ = true;
    resetMessageScroll_ = true;
    lv_indev_reset(nullptr, nullptr);
  }
  if (terminal(nextStatus) != terminal(status_)) rebuild_ = true;
  id_ = nextId; direct_ = direct; actions_ = next; status_ = nextStatus;
  sessionId_ = nextSession; sessionName_ = nextSessionName;
  stateLabel_ = nextId.empty() ? "" : text(ui["stateLabel"]);
  hideStatus_ = !nextId.empty() && ui["stateLabel"].is<const char*>() && stateLabel_.empty();
  title_ = nextId.empty() ? "" : text(ui["title"]);
  message_ = nextId.empty() ? text(doc["agent"]["message"], "操作を待っています") : text(ui["message"]);
  error_.clear(); dirty_ = true;
  refresh(); return true;
}

bool RemoteUi::selectorOpen() const {
  return selector_ && static_cast<lv_dropdown_ext_t*>(lv_obj_get_ext_attr(selector_))->page;
}
void RemoteUi::syncSelectorShield() {
  if (!selectorShield_) return;
  const bool open = selectorOpen() || selectorShieldPressed_;
  if (lv_obj_get_hidden(selectorShield_) == !open) return;
  lv_obj_set_hidden(selectorShield_, !open);
  if (open) {
    // LVGL's dropdown defocuses before the underlying button gets PRESSED.
    // Consume that first outside touch with a transparent LVGL object instead.
    lv_obj_move_foreground(selectorShield_);
    auto* list = static_cast<lv_dropdown_ext_t*>(lv_obj_get_ext_attr(selector_))->page;
    if (list) lv_obj_move_foreground(list);
  }
}
void RemoteUi::sessionEvent(lv_obj_t* object, lv_event_t code) {
  auto* ui = static_cast<RemoteUi*>(lv_obj_get_user_data(object));
  if (!ui) return;
  if (object == ui->selector_ && code != LV_EVENT_RELEASED && code != LV_EVENT_CLICKED && code != LV_EVENT_VALUE_CHANGED) return;
  if (object == ui->selectorShield_) {
    if (code == LV_EVENT_PRESSED) ui->selectorShieldPressed_ = true;
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
      ui->selectorShieldPressed_ = false;
      lv_dropdown_close(ui->selector_);
    }
  }
  ui->syncSelectorShield();
}
bool RemoteUi::enabled() const { return powered_ && connected_ && !id_.empty() && pending_.empty() && !terminal(status_); }
bool RemoteUi::requiresHold(const Action& a) const {
  return direct_ && a.button.size() >= 5 && a.button.compare(a.button.size() - 5, 5, "-hold") == 0;
}
void RemoteUi::queue(const Action& action, const std::string& button) {
  JsonDocument doc;
  doc["type"] = "uiAction"; doc["uiId"] = id_; doc["actionId"] = action.id;
  doc["button"] = button;
  outgoing_.clear(); serializeJson(doc, outgoing_);
  pending_ = action.label; error_.clear(); rebuild_ = true; dirty_ = true;
}
void RemoteUi::activate(int index, bool held, const char* physicalButton) {
  if (!enabled() || selectorOpen() || index < 0 || index >= static_cast<int>(actions_.size())) return;
  const auto& action = actions_[index];
  if (requiresHold(action) && !held) return;
  queue(action, physicalButton ? physicalButton : direct_ ? action.button : "A");
}
void RemoteUi::event(lv_obj_t* object, lv_event_t code) {
  auto* binding = static_cast<Binding*>(lv_obj_get_user_data(object));
  if (!binding) return;
  auto& ui = *binding->owner;
  if (code == LV_EVENT_PRESSED) binding->pressedGeneration = ui.generation_;
  if (binding->generation != ui.generation_ || binding->pressedGeneration != ui.generation_ || !ui.enabled()) return;
  const bool hold = ui.requiresHold(ui.actions_[binding->index]);
  if ((hold && code == LV_EVENT_LONG_PRESSED) || (!hold && code == LV_EVENT_CLICKED))
    ui.activate(binding->index, hold);
}
void RemoteUi::sideButton() {
  if (selectorOpen()) { lv_dropdown_close(selector_); syncSelectorShield(); return; }
  if (!enabled()) return;
  for (size_t i = 0; i < actions_.size(); ++i) {
    const auto& a = actions_[i];
    if (direct_ ? a.button == "P" : a.id == "back" || a.id == "cancel" || a.id == "no" || a.id == "ng") {
      activate(i, false, "P"); return;
    }
  }
}
void RemoteUi::scroll(int pixels) {
  if (!powered_ || !connected_ || selectorOpen()) return;
  if (messagePage_ && lv_obj_get_height(messageLabel_) > lv_obj_get_height(messagePage_))
    lv_page_scroll_ver(messagePage_, -pixels);
  else if (page_ && enabled()) lv_page_scroll_ver(page_, -pixels);
}
std::string RemoteUi::takeAction() { auto result = outgoing_; outgoing_.clear(); return result; }

void RemoteUi::refresh() {
  if (!root_) return;
  syncSelectorShield();
  if (!dirty_) return;
  dirty_ = false;
  lv_label_set_text(link_, demo_ ? "DEMO" : connected_ ? "CONNECTED" : "OFFLINE");
  const auto battery = std::string(charging_ ? "+" : "") + std::to_string(battery_) + "%";
  lv_label_set_text(batteryLabel_, battery.c_str());
  const bool session = !sessionId_.empty() && !sessionName_.empty();
  lv_obj_set_style_local_border_color(root_, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(frameColour(status_)));
  for (auto* obj : {selector_, sessionLabel_, sessionIcon_, selectorArrow_}) lv_obj_set_hidden(obj, !session);
  if (powered_ && connected_) lv_obj_clear_state(selector_, LV_STATE_DISABLED);
  else lv_obj_add_state(selector_, LV_STATE_DISABLED);
  if (session && selectorOptions_ != sessionName_) {
    selectorOptions_ = sessionName_;
    lv_dropdown_set_options(selector_, optionName(selectorOptions_).c_str());
    lv_label_set_text(sessionLabel_, sessionName_.c_str());
  }
  lv_label_set_text(sessionIcon_, statusIcon(status_));
  lv_obj_set_style_local_text_color(sessionIcon_, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(accent(status_)));
  lv_obj_set_style_local_border_color(selector_, LV_DROPDOWN_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(accent(status_)));
  lv_obj_set_hidden(titleLabel_, session || title_.empty());
  lv_label_set_text(titleLabel_, title_.c_str());
  const bool show = enabled() && !actions_.empty();
  int bodyY = session ? 86 : 38;
  if (!session && !title_.empty()) bodyY += 26;
  lv_obj_set_pos(statusLabel_, 14, bodyY);
  lv_obj_set_hidden(statusLabel_, session || hideStatus_);
  if (!session && !hideStatus_) bodyY += stateLabel_.empty() ? 34 : 30;
  const bool paired = actions_.size() == 2;
  const int actionHeight = show ? (actions_.size() <= 2 ? 44 : 80) : 0;
  const int actionY = 228 - actionHeight;
  lv_obj_set_pos(page_, 12, actionY);
  lv_obj_set_size(page_, 216, actionHeight ? actionHeight : 1);
  lv_obj_set_pos(messagePage_, 12, bodyY);
  lv_obj_set_size(messagePage_, 216, std::max(28, (show ? actionY - 8 : 228) - bodyY));
  lv_obj_set_style_local_text_font(statusLabel_, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT,
    stateLabel_.empty() ? &vibe_status_24 : &vibe_jp_16);
  lv_label_set_text(statusLabel_, stateLabel_.empty() ? statusName(status_) : stateLabel_.c_str());
  lv_obj_set_style_local_text_color(statusLabel_, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(accent(status_)));
  const auto messageText = !error_.empty() ? error_ :
    !pending_.empty() ? pending_ + "を送信中…" : message_;
  if (messageText != lv_label_get_text(messageLabel_)) {
    lv_label_set_text(messageLabel_, messageText.c_str());
    resetMessageScroll_ = true;
  }
  lv_obj_set_style_local_text_color(messageLabel_, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT,
    lv_color_hex(0xecf1ec));
  if (resetMessageScroll_) {
    lv_obj_set_y(lv_page_get_scrollable(messagePage_), 0);
    resetMessageScroll_ = false;
  }
  if (rebuild_) {
    lv_indev_reset(nullptr, nullptr);
    lv_obj_clean(lv_page_get_scrl(page_));
    std::fill(std::begin(buttons_), std::end(buttons_), nullptr);
    if (show) for (size_t i = 0; i < actions_.size(); ++i) {
      auto* button = lv_btn_create(page_, nullptr);
      buttons_[i] = button;
      bindings_[i] = {this, static_cast<int>(i), generation_, generation_ - 1};
      lv_obj_set_user_data(button, &bindings_[i]);
      lv_obj_set_event_cb(button, event);
      lv_obj_set_size(button, paired ? 106 : 212, actions_.size() <= 2 ? 44 : 25);
      lv_obj_set_pos(button, paired ? static_cast<int>(i) * 110 : 0,
        actions_.size() <= 2 ? 0 : static_cast<int>(i) * 28);
      lv_page_glue_obj(button, true);
      lv_obj_clean_style_list(button, LV_BTN_PART_MAIN);
      background(button, i == 0 ? 0xe0e6df : 0x1b251f);
      lv_obj_set_style_local_radius(button, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 6);
      lv_obj_set_style_local_border_width(button, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 1);
      lv_obj_set_style_local_border_opa(button, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
      lv_obj_set_style_local_border_color(button, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(i == 0 ? 0xa4b0a7 : 0x68796c));
      lv_obj_set_style_local_bg_color(button, LV_BTN_PART_MAIN, LV_STATE_PRESSED, lv_color_hex(0x87998b));
      lv_obj_set_style_local_text_color(button, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(i == 0 ? 0x142019 : 0xecf1ec));
      lv_obj_set_style_local_text_font(button, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, &vibe_jp_16);
      lv_btn_set_layout(button, LV_LAYOUT_OFF);
      auto* caption = lv_label_create(button, nullptr);
      lv_label_set_long_mode(caption, LV_LABEL_LONG_DOT);
      lv_label_set_align(caption, LV_LABEL_ALIGN_CENTER);
      lv_obj_set_size(caption, paired ? 94 : 198, vibe_jp_16.line_height);
      const auto title = actions_[i].label + (requiresHold(actions_[i]) ? " 長押し" : "");
      lv_label_set_text(caption, title.c_str());
      lv_obj_align(caption, button, LV_ALIGN_CENTER, 0, 0);
    }
    rebuild_ = false;
  }
  lv_obj_set_hidden(page_, !show);
}

std::string RemoteUi::snapshot() const {
  JsonDocument doc;
  doc["uiId"] = id_; doc["state"] = status_; doc["title"] = title_; doc["message"] = message_;
  const auto stateLabel = stateLabel_.empty() ? statusName(status_) : stateLabel_;
  doc["stateLabel"] = stateLabel;
  doc["error"] = error_; doc["pending"] = pending_; doc["revision"] = generation_;
  doc["label"] = (sessionName_.empty() ? "" : sessionName_ + "。") + (hideStatus_ ? "" : stateLabel + "。") + message_;
  doc["displayMessage"] = lv_label_get_text(messageLabel_);
  doc["frameColor"] = frameColour(status_);
  doc["backgroundColor"] = 0x090e0d;
  if (!sessionId_.empty() && !sessionName_.empty()) {
    auto session = doc["session"].to<JsonObject>();
    session["id"] = sessionId_; session["name"] = sessionName_;
    session["open"] = selectorOpen();
    session["icon"] = status_ == "running" ? "play" : status_ == "waiting" ? "question" : status_ == "failed" ? "warning" : "pause";
    lv_area_t area; lv_obj_get_coords(selector_, &area);
    session["x"] = area.x1; session["y"] = area.y1;
    session["width"] = lv_obj_get_width(selector_); session["height"] = lv_obj_get_height(selector_);
  }
  auto body = doc["body"].to<JsonObject>();
  body["x"] = lv_obj_get_x(messagePage_); body["y"] = lv_obj_get_y(messagePage_);
  body["width"] = lv_obj_get_width(messagePage_); body["height"] = lv_obj_get_height(messagePage_);
  body["contentHeight"] = lv_obj_get_height(messageLabel_);
  body["scrollY"] = lv_obj_get_y(lv_page_get_scrollable(messagePage_));
  doc["holdMs"] = holdMs;
  auto nodes = doc["nodes"].to<JsonArray>();
  if (powered_ && connected_ && !sessionId_.empty() && !sessionName_.empty()) {
    auto node = nodes.add<JsonObject>();
    lv_area_t area; lv_obj_get_coords(selector_, &area);
    node["id"] = "session-selector"; node["actionId"] = "session-selector";
    node["label"] = "セッションを選択：" + sessionName_;
    node["x"] = area.x1; node["y"] = area.y1;
    node["width"] = lv_obj_get_width(selector_); node["height"] = lv_obj_get_height(selector_);
    if (selectorOpen()) {
      auto* list = static_cast<lv_dropdown_ext_t*>(lv_obj_get_ext_attr(selector_))->page;
      auto option = nodes.add<JsonObject>();
      lv_obj_get_coords(list, &area);
      option["id"] = "session-current"; option["actionId"] = "session-current";
      option["label"] = sessionName_;
      option["x"] = area.x1; option["y"] = area.y1;
      option["width"] = lv_obj_get_width(list); option["height"] = lv_obj_get_height(list);
    }
  }
  if (enabled() && !selectorOpen()) for (size_t i = 0; i < actions_.size(); ++i) {
    if (!buttons_[i]) continue;
    lv_area_t area; lv_obj_get_coords(buttons_[i], &area);
    lv_area_t viewport; lv_obj_get_coords(page_, &viewport);
    const int top = std::max<int>(viewport.y1, area.y1), bottom = std::min<int>(viewport.y2, area.y2);
    if (bottom - top < 10) continue;
    auto node = nodes.add<JsonObject>();
    node["id"] = std::to_string(generation_) + ":" + std::to_string(i);
    node["actionId"] = actions_[i].id;
    node["label"] = actions_[i].label + (requiresHold(actions_[i]) ? "（0.9秒長押し）" : "");
    node["x"] = area.x1; node["y"] = top;
    node["width"] = area.x2 - area.x1 + 1; node["height"] = bottom - top + 1;
  }
  std::string result; serializeJson(doc, result); return result;
}
}
