# Codex Hook／T-Watchタッチ経路の旧構成

この文書は、T-WatchのタッチUIとHook経由のYES／NO・実行承認を実装した時点の記録です。
単一セッション、タッチ／長押し、シミュレータ一括起動に関する説明は、この旧構成の範囲です。
現在の共通ブリッジは[Codex状態ブリッジ](docs-codex-bridge.md)、Plus2の縦画面は
[CompactUi](compact-ui/README.md)、実機導入は
[GarVibeRemote-M5StickC](https://github.com/ThousandsOfTies/GarVibeRemote-M5StickC/blob/main/README.md)を参照してください。
Plus2へ以下のHook実行承認設定やT-Watch用起動処理を移植しないでください。

Codexのライフサイクルイベントを受け取り、Vibe Remoteの`hello` / `ack` /
`state`形式で配信するローカルサーバーです。VS Codeを起動する必要はありません。

```text
Codex Hooks
  → scripts/codex-hook.py（イベントのメタデータだけを送信）
  → dist/codexBridge.js（HTTP /events → WebSocket state）
  → T-WatchシミュレータのVibeConnection
  → 実機と共通のRemoteUi.cpp / LVGL
```

## 起動

このAppでは`npm ci`、`npm run compile`の後に、秘密の設定ファイルを指定します。

```sh
npm run codex:bridge -- --config /absolute/path/to/local.json
```

設定例（実際のトークンはGit管理・公開パネルの外へ保存）:

```json
{
  "port": 39275,
  "token": "replace-with-a-random-private-token",
  "sessionId": "optional-codex-task-id",
  "label": "監視する作業",
  "freshnessMs": 90000
}
```

T-Watch Productの`scripts/codex_simulate.py`は、この設定の生成、ブリッジの
ビルド・起動、既存のシミュレータへの接続をまとめて行います。

## Codexへの登録

送信スクリプトは[Codexの公式Hooks仕様](https://learn.chatgpt.com/docs/hooks)の
JSON標準入力を受け取ります。通常はイベントごとの送信結果に`{}`を返し、Codexの
承認・実行・継続を変更しません。`PermissionRequest`に限り、後述する明示的な
`--watch-input`設定でWatchの一回限りの許可・拒否を返せます。
サーバー停止時も通常は成功扱いで終了します。
`--strict`は接続試験専用で、配信に失敗した場合に終了コード1を返します。
送信処理は通常実行を`delivery: hook`、`--strict`での試験を`delivery: test`として
区別します。`StatusSnapshot`は常に単発確認として扱います。

`UserPromptSubmit` / `PreToolUse` / `PostToolUse`は、大きな状態欄を「実行中」とし、
その下の説明欄に「ユーザー指示」「ツール実行前」「ツール実行後」を表示します。
`agent.message`には従来どおり「実行中／詳細 · 作業名」を残し、時計の`ui.message`には
詳細だけを渡すことで重複表示を防ぎます。実行状態の値は`running`のままです。`PermissionRequest`は
承認待ち、`Stop`は応答終了（`idle`）、`Interrupt`は中断として表示します。
`Stop`から作業全体の成功を推定しません。コンパクションによる`SessionStart`で
実行状態を初期化することもありません。

Hooksは新規・変更後にCodex側で定義のレビューと信頼設定が必要です。設定ファイルを
生成しただけでは自動実行を確認したことにはなりません。CLIでは対象のプロジェクトを
開いて`/hooks`から確認できます。信頼チェックは無効化しません。

この環境では実行中のデスクトップアプリへCLIの共有サーバー経由で接続する試みが
失敗したため、その経路で常時監視できるとは扱っていません。常時配信はHooksを使います。

## データと期限

状態監視で送信・保存するのはセッションID、ターンID、イベント名、ツール名、作業フォルダー、
発生時刻などのメタデータです。プロンプト、実行コマンド、応答本文、会話ファイルは
送信しません。時計には設定した短い作業名と状態を表示します。

`sessionId`を設定すると他のタスクのイベントは表示を変えません。同じタスクの
以前のターンの終了や古い発生時刻も現在の状態を上書きしません。
更新が`freshnessMs`を超えて途絶えると「状態更新なし」を表示します。
WebSocketのpingによってCodexイベントの鮮度を延長しません。
`state.monitor`に更新元（`waiting` / `hooks` / `test` / `snapshot`）、観測時刻、
鮮度を付けます。WSの接続表示とCodexの状態更新を別々に表示するための情報です。

`scripts/codex-diagnostics.py`は公式App Serverの`initialize`と`hooks/list`だけで
指定したプロジェクトの送信設定を確認します。新しいチャットやターンを作らず、
信頼・承認設定を書き換えません。結果には該当設定の件数と信頼状態だけを返し、
トークン・コマンド・他のHooksの内容はパネルへ渡しません。

`StatusSnapshot`はこのブリッジ専用の入力形式です。Codexの公式Hook名ではありません。
アプリツールで読み取った実際のタスク状態を、一回送信する接続試験に使えます。
`active` / `idle` / `notLoaded` / `systemError`と、取得元が提供する待機フラグを
変換します。この一回の送信を継続的なイベント監視とは扱いません。

## Watchから質問に回答する

秘密の設定に`watchInput: true`と対象の`sessionId`を指定すると、認証付き
`POST /interactions`でYES / NOの質問を表示できます。本文は`kind: question`、
`sessionId`、`message`、任意の`turnId`、`timeoutMs`（1〜120秒）です。
作成応答の`id`と所有者用の`receipt`で`POST /interactions/ID/poll`を呼びます。
`receipt`は端末の表示・WebSocketへ配信しません。`.../cancel`で取り消せます。

Codexからは質問ツールを呼び、戻ったJSONを回答として使います。

```sh
python scripts/watch-input.py --config /private/local.json --question "この内容で続けますか？"
```

`CODEX_THREAD_ID`または`--session-id`で呼び出し元のチャットを指定します。
成功時は`status: answered`と`answer: yes` / `answer: no`、回答した端末名が返ります。
NOも正常な回答です。未回答は`expired` / `cancelled` / `unavailable`として区別します。
通常の`request_user_input`画面を自動転送するものではなく、YES / NOの問いをこの
ツール経由で送ります。自由文や3個以上の選択肢はPC側の質問UIを使用します。

## Watchから実行を許可・拒否する

`PermissionRequest`のHookコマンドだけに`--watch-input`を付け、同期実行の
`timeout`を125秒にします。他のイベントは従来の3秒のままです。
これは[公式PermissionRequestの応答仕様](https://learn.chatgpt.com/docs/hooks#permissionrequest)を使います。
既存の承認ポリシーや信頼記録は書き換えません。

時計にはツール名、作業フォルダー、**実際の`tool_input`のJSON**を表示します。
質問・承認の本文はローカルの時計と接続中のシミュレータへ配信し、メモリ上でのみ扱います。
1000文字を超える承認内容は切り詰めずPCへ戻します。長い本文は折り返して表示し、
上下になぞって縦スクロールできます。YES / NOは下部に固定します。
PCから送る`state.session`の`id`と`name`で、状態アイコン付きのセッション選択欄を表示します。
名前は表示幅に合わせて省略します。状態は▶（実行中）、?（入力待ち）、一時停止（待機）で示し、
背景は黒、4 pxの外枠だけを深緑／暗赤／濃灰に変えます。
現時点のbridgeは選択済みの1セッションのみを通知します。質問・承認・回答後にも同じ名前を保持し、
ドロップダウンを開閉しても入力先は変わりません。名前には起動時の`--label`を使用します。
YESの**900ms長押し**で今回の操作だけを`allow`、NOのタッチまたは側面ボタンで`deny`にします。
永続的な許可や後続の操作の承認には使いません。

回答を受け取った後は、時計に「YESを送信しました」または「NOを送信しました」を
表示します。CODEXの見出し、共通の完了挨拶、常設の戻る案内は表示しません。

120秒で未回答なら`{}`を返して通常のPC側承認へ戻します。切断・サーバー停止・
別セッション・取り消しも許可や拒否に置き換えません。短いタッチではYESを送信しません。
実際のCodexが承認を要求したときだけ動き、承認不要の操作から疑似的な要求を作りません。
変更したHookはCodexの`/hooks`で新しい定義をレビュー・信頼してから有効になります。

同時に扱うのは一件です。質問IDは毎回ランダムに生成し、認証して`hello`を済ませた
端末からの、そのIDに対応する一回答だけを受け付けます。呼び出し元は5秒以内ごとに
pollして待機を維持します。呼び出し元の消失、新しいターン、応答終了、中断、セッション終了は
未処理の要求を無効にします。古い画面、重複した回答、期限切れの回答は拒否します。

### 入力テスト専用の例外

認証付き`POST /input-test`へ空のJSONオブジェクト`{}`を送ると、固定の接続試験を
時計へ表示します。`GET /input-test`で質問ID、期限、状態、YES / NOの結果を確認できます。
実行中の試験と同じIDを持つ`uiAction`の`yes` / `no`だけを、一度限り受け付けます。
試験は2分で期限切れになり、終了後8秒で通常の状態表示に戻ります。
Codexの状態ストアは変更せず、実行の許可・拒否や会話への入力も行いません。

## 検証

```sh
npm run compile
npm run typecheck
npm run lint
npm test
```

`test/codexBridge.test.ts`と`test/codexInteraction.test.ts`は状態変換、タスクとターンの
隔離、鮮度、HTTP認証、WebSocketの認証・配信、要求と回答の期限・取り消しを確認します。
Product側の`tests/codex-simulator.test.mjs`はPython送信処理からWebSocket、
既存の接続adapter、共通LVGL WebAssemblyまで通して表示と再接続を確認します。
Product側の`tests/watch-interaction.test.mjs`は実際のPython Hookと質問ツールを起動し、
共通LVGLのタッチ・長押しからYES / NOを戻して、Codex向けの応答JSONまで確認します。
