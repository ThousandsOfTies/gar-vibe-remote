#include "CompactUi.cpp"
namespace { vibe::CompactUi ui; std::string snapshot,outgoing; }
extern "C" {
void gar_app_init(){ui.begin();}
int gar_app_receive(const char* json){return ui.receive(json);}
void gar_app_hardware(int powered,int connected,int battery){ui.hardware(powered,connected,battery);}
void gar_app_button(int key,int phase){ui.button(static_cast<char>(key),phase);}
void gar_app_refresh(){ui.refresh();}
const char* gar_app_snapshot(){snapshot=ui.snapshot();return snapshot.c_str();}
const char* gar_app_take_action(){outgoing=ui.takeAction();return outgoing.c_str();}
}
