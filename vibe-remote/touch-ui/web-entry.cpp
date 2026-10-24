#include "RemoteUi.h"
#include "RemoteUi.cpp"
namespace { vibe::RemoteUi ui; std::string snapshot, outgoing; }
extern "C" {
void gar_app_init() { ui.begin(); }
int gar_app_receive(const char* json) { return ui.receive(json, strlen(json)); }
void gar_app_hardware(int powered, int connected, int battery, int charging, int demo) {
  ui.hardware(powered, connected, battery, charging, demo);
}
void gar_app_reset() { ui.reset(); }
void gar_app_refresh() { ui.refresh(); }
void gar_app_side_button() { ui.sideButton(); }
void gar_app_scroll(int pixels) { ui.scroll(pixels); }
const char* gar_app_snapshot() { snapshot = ui.snapshot(); return snapshot.c_str(); }
const char* gar_app_take_action() { outgoing = ui.takeAction(); return outgoing.c_str(); }
}
