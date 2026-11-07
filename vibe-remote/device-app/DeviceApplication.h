#pragma once
#include <functional>
#include <string>
#include "CompactUi.h"
#include "DeviceLink.h"
#include "PairingConsent.h"
#include "VoiceInput.h"
#include <memory>

namespace vibe {
// Application runtime shared by ESP32 and the PC simulator. The platform owns
// byte transport, LVGL display/clock, battery readings and Bluetooth callbacks.
// It does not interpret application packets or make pairing/choice decisions.
class DeviceApplication {
 public:
  struct Hardware {
    bool powered=true, sppReady=false, sppClient=false;
    int battery=0, width=0, height=0, board=0;
    bool bleReady=false, bleClient=false, charging=false;
  };
  struct Identity {
    std::string device, firmware, bluetoothName, sourcePrefix;
    std::string pairingMode="ssp-numeric-comparison", pairingInstruction="CでYESを選び、Aで決定してください。";
  };
  using Send = std::function<void(Transport,const std::string&)>;
  using ConfirmPairing = std::function<void(bool)>;
  DeviceApplication(Identity identity,Send send,ConfirmPairing confirm);
  void setInputHints(const CompactUi::InputHints& hints) { ui_.setInputHints(hints); }
  void begin(const Hardware& hardware);
  void hardware(const Hardware& hardware);
  void feed(Transport link,const char* bytes,size_t size);
  // An epoch boundary resets a stream and releases its ownership. Overflow
  // instead discards through the next newline, without joining broken frames.
  void resetTransport(Transport link);
  void discardTransport(Transport link);
  void button(char key,int phase);
  void enableVoice(int16_t* buffer,size_t capacity,VoiceInput::Mic mic);
  void voiceAudio(const int16_t* pcm,size_t count) {if(voice_)voice_->audio(pcm,count,lv_tick_get());}
  void pairingRequested(uint32_t number);
  void authenticated(bool success);
  bool allSessionsIdle() const {return !(voice_ && voice_->active()) && !pairing_.active() && ui_.allSessionsIdle();}
  void step();
  std::string snapshot() const;
 private:
  vibe::CompactUi ui_;
  std::unique_ptr<VoiceInput> voice_;
  JsonLines usbLines_,sppLines_,bleLines_;
  DeviceLease lease_;
  PairingConsent pairing_;
  Hardware hardware_;
  Identity identity_;
  Send send_;
  ConfirmPairing confirm_;
  bool pairingScreenVisible_=false;
  unsigned authenticationAttempts_=0;
  int lastAuthentication_=0;
  void updateHardware();
  void send(Transport link,const JsonDocument& document) const;
  void receive(Transport link,const std::string& packet);
  void hello(Transport link) const;
  void pairingInfo(JsonDocument& document) const;
  void pairingScreen(const char* message,bool choices=false);
};
}
