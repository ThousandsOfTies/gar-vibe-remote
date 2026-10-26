# Vibe Remote — Current Handoff

更新日：2026-10-26。

## 現在の対象

GarVibeRemote-M5StickCはM5StickC Plus2をUSB／Bluetooth Classic SPPでPCにつなぎ、
ローカルCodexの状態監視と選択式質問への回答を行います。最大16チャットを扱います。
Cで選択、Aを押して離すと決定、Bでセッション一覧／戻る。本文は自動スクロールします。

## 作業入口

- [Product README](https://github.com/ThousandsOfTies/GarVibeRemote-M5StickC/blob/main/README.md)：導入、実機、接続、検証の全手順。
- [Codexブリッジ](../vibe-remote/docs-codex-bridge.md)：共有PCサーバーのAPI・責務。
- [CompactUi](../vibe-remote/compact-ui/README.md)：実機とWASMで共通の画面・操作。
- [VS Code連携](../vibe-remote/README.md)：別の任意機能。Codex Productの導入には不要。

実機ファームウェアはProductの`firmware/stickc-plus2/`です。
`DeviceLink.h`がJSON Lines・操作権・接続期限、`PairingConsent.h`が番号照合の状態を扱い、
画面・スクロール・ボタン操作は共有`compact-ui/`で扱います。

PC側の実機中継、チャット収集、質問処理、閲覧用配信、静的HTTP配信はProductの別処理です。
共有Appのサーバーからそれらを起動したり、Hookを変更したりしません。

## 通信の範囲

デバイス接続はUSB／SPPです。Wi-Fiファームウェア、LAN待受、mDNS、Local Bridgeは削除済みです。
PC上のWebSocketはlocalhostで使用し、トークンを実機・ブラウザへ渡しません。
T-Watchのタッチ・長押し操作の記録は[旧構成](../vibe-remote/docs-codex-touch-legacy.md)へ分けています。
現在のPlus2の操作や導入には流用しません。

共有Appは`npm run compile`、`npm run typecheck`、`npm run lint`、`npm test`で検証します。
Product側の検証はProduct READMEに従い、共有変更を先にコミット・プッシュしてからProductの固定コミットを更新します。
