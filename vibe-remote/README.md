# Vibe Remote — Shared Bridge / VS Code Integration

このパッケージは[Codex状態ブリッジ](docs-codex-bridge.md)と
[CompactUi](compact-ui/README.md)を提供します。M5StickC Plus2の導入・実機接続は
[GarVibeRemote-M5StickC](https://github.com/ThousandsOfTies/GarVibeRemote-M5StickC/blob/main/README.md)の手順を使用します。

以下は別の任意機能であるVS Code拡張の説明です。Codex Productの導入には不要です。

## VS Code拡張

- PC内のWebSocketサーバー（`127.0.0.1:39271`）。認証トークンを使います。
- エディター、タスク、デバッグ等の観測できる状態と、状態ビューアの表示。
- 任意のBluetooth Classic SPPシリアル接続。
- 任意のWindows UI Automation承認ブローカー。

Wi-Fiクライアント、LANへの公開設定、自動探索、Local Bridgeはありません。
VS Codeの安定APIでは一般のチャット内容・エージェントの質問を取得しません。
Codex Appの質問連携はProduct側の実装を使います。

## コマンド

- `Vibe Remote: 状態ビューアを開く`
- `Vibe Remote: 接続トークンを表示`
- `Vibe Remote: 接続トークンを再生成`
- `Vibe Remote: サーバを再起動`
- `Vibe Remote: Approval Brokerを開始`
- `Vibe Remote: Approval Brokerを停止`
- `Vibe Remote: Approval Brokerの状態を表示`

## 設定

| 設定                                                                           | 用途                                                  |
| ------------------------------------------------------------------------------ | ----------------------------------------------------- |
| `vibeRemote.port`                                                              | PC内のWebSocketポート。既定は39271                    |
| `vibeRemote.token`                                                             | 接続トークン。空ならVS Code SecretStorageで生成・保持 |
| `vibeRemote.idleThresholdMs` / `pollIntervalMs`                                | 状態の観測・配信間隔                                  |
| `vibeRemote.sppEnabled` / `sppPort` / `sppBaudRate`                            | 任意のシリアルSPP接続                                 |
| `vibeRemote.approvalBroker.enabled` / `dryRun`                                 | 任意のVS Code承認監視と動作試験                       |
| `vibeRemote.approvalBroker.port`                                               | PC内の接続先。未指定なら`vibeRemote.port`             |
| `vibeRemote.approvalBroker.pollSeconds` / `decisionTimeoutSeconds` / `logPath` | 承認監視の間隔・期限・ログ                            |

SPP設定はこの拡張のプロトコルを使う端末向けです。現在のPlus2のCompactUiへは、
Productの`device_bridge.cjs`がUSB／SPPで接続します。

承認ブローカーは初期状態で無効、`dryRun`は有効です。動作試験では画面の検出だけを行い、
ユーザーが有効化した場合に限り、実際のVS CodeのAllow／Skip操作を行います。
この機能をCodex Appの選択式質問と同じ経路として扱いません。

## ビルドと検証

```sh
npm ci
npm run compile
npm run typecheck
npm run lint
npm test
```

拡張の生成先は`dist/extension.js`、Codexブリッジは`dist/codexBridge.js`です。
VS Code Remote WSL向けの`scripts/npm.sh`はビルド用Node.jsの選択を行う補助で、デバイスの通信中継は行いません。
