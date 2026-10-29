# gar-vibe-remote

Vibe Remoteの共有コードです。Codexの状態・質問ブリッジと、M5StickC Plus2向けの縦画面UIを提供します。
実機はBluetooth Classic SPPまたはUSBシリアルでPCへ接続します。

| 用途                                            | 入口                                                                                                    |
| ----------------------------------------------- | ------------------------------------------------------------------------------------------------------- |
| Plus2の導入・書き込み・ペアリング               | [GarVibeRemote-M5StickC](https://github.com/ThousandsOfTies/GarVibeRemote-M5StickC/blob/main/README.md) |
| Codexの状態・複数チャット・選択式質問           | [Codexブリッジ](vibe-remote/docs-codex-bridge.md)                                                       |
| 135×240の画面・スクロール・A／B／C操作          | [CompactUi](vibe-remote/compact-ui/README.md)                                                           |
| PC内のVS Code状態ビューア・任意の承認ブローカー | [VS Code連携](vibe-remote/README.md)                                                                    |
| T-WatchのタッチUI・Hook実行承認の記録           | [旧構成](vibe-remote/docs-codex-touch-legacy.md)                                                        |

実機のファームウェア、USB／SPP中継、Codex Appの収集アダプターはProductに置きます。
この共有リポジトリから実機へ書き込む専用ファームウェアは提供しません。
Wi-Fi端末、LAN向け待受、mDNS広告、Local Bridgeは削除しました。
PC内の認証付きWebSocketは、状態ブリッジ・実機中継・閲覧用シミュレータの間で使用します。

## 構成

- `vibe-remote/src/codexBridge.ts`：localhostのHTTP／WebSocketサーバー。
- `vibe-remote/src/codexStatus.ts`、`codexSessions.ts`、`codexChatQuestions.ts`：状態・複数チャット・質問の管理。
- `vibe-remote/compact-ui/`：Plus2実機とWebAssemblyで共通のLVGL画面。
- `vibe-remote/touch-ui/`：旧タッチUIと、CompactUiが共有する日本語フォント。
- `vibe-remote/src/extension.ts`：任意のVS Code拡張。PC内の状態ビューアとSPP接続。
- `docs/PROJECT_HANDOFF.md`：共有コードとProductの責務・作業入口。

## ビルドと検証

```sh
cd vibe-remote
npm ci
npm run compile
npm run typecheck
npm run lint
npm test
```

Plus2への導入はProductの手順を使います。Productはsubmoduleのコミットを固定し、
`firmware/stickc-plus2/platformio.ini`を使って共有UIを実機へ組み込みます。

## ライセンス

独自コードと文書は[MIT License](LICENSE)です。
`vibe-remote/package.json`の`license: MIT`と同じ利用条件を、このリポジトリのLICENSEに明記しています。
日本語フォントのデータは[SIL Open Font License 1.1](vibe-remote/touch-ui/OFL.txt)です。
その他の依存ライブラリは、それぞれのライセンスに従います。
