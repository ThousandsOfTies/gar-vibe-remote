# M5StickC Wi-Fi Client Handoff (legacy implementation)

対象: このディレクトリのWi-Fi／WebSocketクライアント。現在のGarVibeRemote-M5StickC Productとは別実装です。

| 実装                  | ビルドする場所                                                              | 操作                            |
| --------------------- | --------------------------------------------------------------------------- | ------------------------------- |
| 現在のPlus2 Product   | Productの`firmware/stickc-plus2/`と共有[CompactUi](../compact-ui/README.md) | Cで選択、Aで決定、Bで一覧／戻る |
| このWi-Fiクライアント | このディレクトリの`src/main.cpp`                                            | Aで決定、Bで選択、Pで戻る       |

現在のPlus2へ導入する場合は[Product README](https://github.com/ThousandsOfTies/GarVibeRemote-M5StickC/blob/main/README.md)を使用してください。
`src/main.cpp`に集約された描画・Wi-Fi・状態管理はこの旧クライアントのコードで、
現在のPlus2は参照・ビルドしません。旧クライアントは他の利用者向けに残しており、Product側の整理のために削除・改変しません。

## Current Shape

- App root: `vibe-remote/m5stickc-client`
- Source: `src/main.cpp`
- PlatformIO env: `m5stickc-plus2-vibe-min`
- Transport: Wi-Fi + WebSocket
- Discovery: `_vibe-remote._tcp.local` via mDNS
- Local fallback: `VIBE_REMOTE_HOST` / `VIBE_REMOTE_PORT`

## Device UI

The firmware renders the UI payload received from the Vibe Remote bridge and
maps the physical buttons as follows:

| Button | Meaning          |
| ------ | ---------------- |
| A      | Select / confirm |
| B      | Rotate menu      |
| P      | Back             |

Hold variants are sent as `A-hold`, `B-hold`, and `P-hold` when matching
actions exist.

## Local Secrets

Use `.env.local` or environment variables. Do not commit Wi-Fi credentials or
tokens.

```dotenv
VIBE_WIFI_SSID=...
VIBE_WIFI_PASS=...
VIBE_REMOTE_TOKEN=...
```

## Build

```bash
cd vibe-remote/m5stickc-client
make build
make upload
make monitor
```

Package a VM/Codespaces build:

```bash
make vm-package
```
