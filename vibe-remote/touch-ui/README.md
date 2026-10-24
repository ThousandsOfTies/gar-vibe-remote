# Shared LVGL touch UI

`RemoteUi.cpp` owns the 240 × 240 screen, LVGL widget construction, JSON UI state,
and event callbacks. It is compiled unchanged for the T-Watch firmware and the
browser. The existing M5StickC client opts into platform hooks only when
`VIBE_TOUCH_UI_ENABLED` is defined; its default build retains its original UI.

- `lv_btn_create` / `lv_label_create`: objects, labels and styles.
- `lv_obj_set_event_cb`: pointer clicks and LVGL long-press events.
- `lv_dropdown_create`: the current session selector. Opening/closing is local,
  uses LVGL hit testing, and cannot grant or reject a pending question.
- `receive`: server state, acknowledgements and stale-screen cancellation.
- `takeAction`: outgoing protocol JSON, without credentials or transport details.
- `snapshot`: visible object bounds and accessible names obtained from LVGL.
- `web-entry.cpp`: thin C ABI for the same class in WebAssembly.

`ui.stateLabel` optionally supplies a detailed status caption, such as
`実行中／ユーザー指示`. Detailed captions use the 16 px Japanese font to fit
the watch width; omitting it restores the standard 24 px state caption.
An explicit empty caption hides the status row. Empty titles also reclaim their row.
The transport adapter supplies the meaning of the caption.

When `state.session = {id, name}` is supplied, a 44 px dropdown replaces the
title/status rows. The selected name is ellipsized, with separate play,
question-mark or pause icons and a right-hand dropdown arrow. The session name
is retained across question and answer screens. The current Codex bridge sends
one selected session; the dropdown does not imply support for switching input
ownership across chats. A side-button press closes the list before handling NO.
An LVGL overlay consumes outside touches until release, so dismissing the list
cannot click an answer behind it; a separate deliberate touch is required.
The background stays `#090e0d`. A square 4 px frame fills all display corners and indicates running (`#238a43`),
waiting/error (`#63000f`) or idle/finished (`#4d4d4d`), independently of the
readable, brighter icon and selector outline. Failed states retain a warning icon.

Messages wrap in a separate vertically scrollable LVGL page. Dragging the text
moves only that page; one or two answers stay in a fixed bottom row, and longer
action lists keep their own scrollable region. There is no automatic scrolling.
Same-message polling preserves the reading position; a new question resets it.
The browser wheel scrolls overflowing text first, or the action list for short text.
The same layout and touch handling run on the physical watch and in the simulator.
No decorative completion text or permanent back hint is inserted. A side-button
press only sends an explicitly available back, cancel, or NO action.

The Product supplies display/input drivers, authentication, device state and
transport. The browser surface returns mouse, touch and keyboard input through
LVGL's pointer driver. Its transparent HTML buttons mirror LVGL objects; they
do not implement selection or hold decisions. Same-ID polling preserves an
active gesture; screen replacement, disconnect and power-off cancel it.

Use the Product's pinned LilyGO Library (commit
`e5a0f825a21198f97d2bafee03ea853766483d20`), which contains LVGL **7.11.0**.
`vibe_lv_conf.h` extends that library's configuration for both builds. LVGL 8/9
APIs are not interchangeable with this port.

## Fonts

`vibe_jp_16.c` and `vibe_status_24.c` are source assets shared by both builds.
They are generated from Noto Sans CJK JP Regular under the SIL Open Font License;
see `OFL.txt`. Each generated font must be compiled in its own C translation unit.
The 16 px font covers ASCII and Shift-JIS Japanese characters; arbitrary emoji
and characters outside that subset are not included. The 24 px font contains
the fixed status captions.

To regenerate, obtain `NotoSansCJKjp-Regular.otf` from the official
[Noto CJK repository](https://github.com/notofonts/noto-cjk), install
`lv_font_conv@1.5.3`, then run:

```bash
python3 generate_fonts.py --node node \
  --converter /path/to/node_modules/lv_font_conv/lv_font_conv.js \
  --font /path/to/NotoSansCJKjp-Regular.otf
```

Run the Product's `make sim-build` and physical PlatformIO build after changing
UI sources or fonts. `make check-sim` exercises the compiled LVGL event handling.
