# Codex状態ブリッジ

`src/codexBridge.ts`はPC上のローカルサーバーです。状態・質問をHTTPで受け取り、
認証付きWebSocketで`state`／`sessions`を配信し、端末の回答を受け付けます。
VS Codeを起動する必要はありません。実機の接続、チャットの収集、シミュレータ配信はProduct側の別処理です。

## どの実装を使うか

| 用途                              | 実装・手順                                                                                                                                                             | 操作・接続                                      |
| --------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------- |
| 現在のM5StickC Plus2              | [Product導入手順](https://github.com/ThousandsOfTies/GarVibeRemote-M5StickC/blob/main/README.md)、Productの`firmware/stickc-plus2/`、[CompactUi](compact-ui/README.md) | USB／SPP。Cで選択、Aで決定、Bで一覧／戻る       |
| Plus2のWebシミュレータ            | 同じProductの`simulation/`とCompactUiのWASM                                                                                                                            | 実機と同じ画面。`?live=1`は本番状態の閲覧専用   |
| 旧T-WatchのタッチUI・Hook実行承認 | [旧構成の記録](docs-codex-touch-legacy.md)                                                                                                                             | タッチ／長押し。現在のPlus2の手順には使用しない |

Plus2はProduct内の`firmware/stickc-plus2/`と、このAppの`compact-ui/`を組み合わせます。
デバイス接続はUSB／SPPです。旧Wi-FiファームウェアとLAN向けの探索・中継は削除しました。

## 入出力と責務

```text
Productのチャット収集 ── HTTP /sessions ──────────┐
監視用Codex Hook ────── HTTP /events ────────────┤
Productの質問処理 ───── HTTP /chat-questions ─────┤
                                              ▼
                              codexBridge.ts / localhost:39275
                                              │
                              認証付きWS: state / sessions / ack
                                  ┌───────────┴──────────┐
                           Productの実機中継      Productの閲覧用配信
                                  │                     │
                         USB / SPP: JSON Lines      Webシミュレータ
                                  │                 （閲覧専用）
                             Plus2 / CompactUi
                                  │ uiAction
                      実機中継 → 状態ブリッジ → 質問処理 → 元のチャット
```

このAppが提供するのは中央の状態ブリッジと共有UIです。現在のPlus2 Productでは、
`session_monitor.cjs`がチャットの収集、`question_service.cjs`がチャットごとの質問・回答、
`device_bridge.cjs`がUSB／SPP、`simulator_feed.cjs`が閲覧用配信を担当します。
HTTPで静的ファイルを配る処理は、これらを起動・管理しません。

| API                                                              | 用途                                                                |
| ---------------------------------------------------------------- | ------------------------------------------------------------------- |
| `POST /events`                                                   | Hookのライフサイクル、単発の`StatusSnapshot`                        |
| `POST /sessions`                                                 | 複数チャットの一覧・状態を全件スナップショットで更新                |
| `POST /chat-questions`                                           | セッションごとの質問一覧・配信結果を更新し、実機回答を取得          |
| `GET /state`                                                     | 現在配信している状態を取得                                          |
| `POST /interactions`、`POST /interactions/ID/poll`、`.../cancel` | 明示的な単一セッション用の質問・実行承認。Plus2導入では有効化しない |
| `POST /input-test`、`GET /input-test`                            | Codexの操作承認とは独立した、単一監視でのYES／NO接続試験            |

`/health`以外のHTTP APIとWebSocketは認証が必要です。
Plus2のトークンはPCの中継が保持し、実機・公開ファイル・ブラウザには渡しません。

## 起動

通常の実機導入では、Productの
[導入ガイド](https://github.com/ThousandsOfTies/GarVibeRemote-M5StickC/blob/main/docs/INSTALL.md)を使用します。
以下は共有Appのブリッジだけを開発・検証するときの入口です。

```sh
npm ci
npm run compile
npm run codex:bridge -- --config /absolute/path/to/private/local.json
```

最小の単一監視設定例（トークンは実際にはランダムに生成し、公開ディレクトリの外へ保存）:

```json
{
  "port": 39275,
  "token": "replace-with-a-random-private-token",
  "sessionId": "selected-local-chat-id",
  "label": "監視する作業",
  "sessionScope": "single",
  "freshnessMs": 90000,
  "watchInput": false
}
```

ブリッジの起動だけでは、Codexの一覧収集・質問処理・実機中継は始まりません。
`sessionScope`は`single`／`project`／`local`／`manual`です。
複数監視にはProductの収集処理も必要で、最大16チャット、各8選択肢を扱います。
一覧から消えたセッションや期限切れの質問へ、古い回答を振り替えません。

## 現在のPlus2の状態と回答

複数監視ではCodex App Toolsから実行状態を収集します。監視用Hookから届くメタデータは、
「ユーザー指示」「ツール実行前」「ツール実行後」などの補足に使います。
全プロジェクトの収集のために、各プロジェクトへHookを追加する必要はありません。
Hookを使用する場合の定義レビュー・信頼設定は、Productの導入手順に従います。

質問はCodex Appの`request_user_input_async`をProduct側で読み取り、元のチャット・質問・選択肢の
IDを保持して配信します。実機でCを押して選び、Aを押して離すと回答します。
自由記述、同期型`request_user_input`、ツール実行の許可・拒否はPCで扱います。
質問文と選択肢は表示のため配信しますが、通常の状態通知にプロンプトや実行コマンドは含めません。

状態の鮮度、WebSocket接続、質問の更新期限、一覧の更新期限は別々に管理します。
pingは観測したCodex状態の鮮度を延長しません。質問の送達が不明な場合も回答を自動再送しません。
Productのローカル履歴／App ToolsアダプターはCodex Appの版に依存するため、互換性の確認はProduct側で行います。

## コードと検証

| ファイル                    | 責務                                           |
| --------------------------- | ---------------------------------------------- |
| `src/codexBridge.ts`        | HTTP／WebSocket、認証、状態と回答の配送        |
| `src/codexStatus.ts`        | ライフサイクルと状態の鮮度                     |
| `src/codexSessions.ts`      | 複数チャットの一覧・セッション別状態           |
| `src/codexChatQuestions.ts` | 選択式質問、実機回答、質問の更新期限           |
| `src/codexInteraction.ts`   | 明示的な単一セッションの質問・実行承認         |
| `compact-ui/`               | Plus2の画面とA／B／C操作。通信・GPIOは含まない |

```sh
npm run typecheck
npm run lint
npm test
```

共有Appの試験は認証、セッション・質問の隔離、期限、回答の配送を確認します。
Product側のPC・実機・WASMの検証手順は
[Product README](https://github.com/ThousandsOfTies/GarVibeRemote-M5StickC/blob/main/README.md#開発時の検証)を参照してください。
旧T-Watchのタッチ・長押し試験とは別の経路です。
