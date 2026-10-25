# Compact LVGL UI

M5StickC Plus2向け240×135のボタン操作UI。`CompactUi`はLVGL 7とArduinoJson 7だけに依存し、
ブラウザ・通信・GPIO処理を持ちません。実機にはLVGLの表示ドライバと物理ボタンのedgeを接続します。
日本語フォントは既存の`../touch-ui/vibe_jp_16.c`を共有します（同ディレクトリのOFL.txt参照）。

- A短押し：次のページ。最後のページでは回答画面へ。
- B短押し：前のページ。回答画面では短押し送信なし。
- 回答画面：Aで選択、Bを900ms以上押して離すと送信。初期選択は第2候補。
- C短押し：本文の先頭へ。長押し電源はboard側の責務。
- 同じstateの再受信ではページを保持。質問・選択肢・接続の変更、cancelで長押しを破棄。
- 送信後は再送をロック。`uiId`と`actionId`が一致するackのみ受理。
- 一度に表示・回答できるのは1セッションの2候補まで。BLE/SPP transportは別実装。

入力は既存の`state`（`session`, `agent`, `ui`）、出力は`uiAction`。
`ack`は`{type:'ack', uiId, actionId, ok, error?}`。
このack契約を既存PCブリッジへ接続する際は、ブリッジ側との照合が必要です。
