# Portable compact remote application

`DeviceApplication` composes CompactUi, JSONL framing, USB/SPP ownership and
leases, pairing consent, and outgoing answers. It has no Arduino, M5Unified,
GPIO numbers, Bluetooth stack, display driver or board initialization.

The Product supplies `Identity` (device/firmware/displayed Bluetooth name/source
prefix), hardware facts, a byte-send callback and a pairing-confirm callback.
Call `begin` after LVGL/display initialization. Feed explicit-length byte buffers,
button edges, transport reset/overflow, hardware and pairing events on one task.
The platform advances LVGL's clock; call `step` and `lv_task_handler` on that task.
Compile `DeviceApplication.cpp` and `compact-ui/CompactUi.cpp` once each, with
LVGL 7 and ArduinoJson 7. A single runtime serves firmware and a PC/WASM host.

CompactUi owns C/select, A/confirm and B/list behavior. Bluetooth callback
threading, pairing transport, power control and LCD flushing belong to the board
adapter. PC chat discovery, enrollment and the choice of scenarios belong to
the composing Product. No board model is inferred by this application.
