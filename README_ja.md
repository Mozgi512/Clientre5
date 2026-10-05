# Clientre 5

[English](README.md) | **日本語**

[M5Stack Tab5](https://docs.m5stack.com/ja/core/Tab5)（ESP32-P4）用のポケット SSH ターミナル OS です。
Tab5 を単体で使える端末に変えます。物理キーボード中心の UI、日本語入力、Tailscale クライアント、
ファイル・メディアツールを備えています。

![Clientre 5 ホーム画面（Orange EL テーマ）](docs/ui/orange-home.png)

## 主な機能

- **SSH ターミナル**: VT100 / xterm-256color 互換、スクロールバック、全角 CJK 文字対応。
  パスワード・公開鍵・キーボードインタラクティブ認証。ホスト鍵は初回接続時に確認して保存します。
- **日本語入力**: ローマ字→かな、SKK 辞書によるかな漢字変換（SKK-JISYO.M 内蔵、TF カードの
  SKK-JISYO.L も利用可）。UI が中国語のときは拼音入力も使えます。`Ctrl+Space` または `A/あ` キーで切替。
- **Tailscale**: 同梱の [MicroLink](https://github.com/Csontikka/microlink) クライアントで tailnet に参加し、
  ホスト名で SSH 接続できます。Headscale にも対応。
- **キーボード操作**: Tab5 キーボードや USB キーボードで全画面を操作できます（Tab / 矢印 / Enter / Esc）。
  キーボードが無いときはソフトキーボードを使います。
- **ファイル・メディア**: TF カード / USB メモリのファイル管理、JPEG/PNG/BMP/GIF 画像ビューア（EXIF 表示）、
  UTF-8/UTF-16 テキストビューア、MP3/FLAC/WAV プレイヤー（バックグラウンド再生）、TF カードに JPEG で保存するカメラ。
- **Web ファイル**: WiFi 経由でブラウザから TF カードを閲覧・アップロード・ダウンロード。
- **USB ディスクモード**: USB-C で TF カードを PC にストレージとして公開。
- **OTA 更新**: 本体から新しいファームウェアを確認・ダウンロード・インストール。
- **外観**: Orange EL（既定）、Retro CRT、Pixel、Dark、Light、Nord、Solarized Dark、Gruvbox、Classic。
  ターミナルの 16 色パレットまで個別に変更できます。
- **セキュリティ**: SSH のパスワードと鍵は AES-256-GCM で暗号化して保存。サーバー一覧は本体の鍵で暗号化して
  TF カードに置くこともできます。暗号化バックアップに対応。
- **言語**: English、日本語、简体中文、繁體中文。

<p>
  <img src="docs/ui/orange-settings.png" width="49%" alt="設定（Orange EL）">
  <img src="docs/ui/classic-settings.png" width="49%" alt="設定（Classic）">
</p>

*画像はファームウェア UI をホスト PC でレンダリングしたものです（[ui_preview](firmware/tools/ui_preview/README.md)）。*

## 対応ハードウェア

| | |
|---|---|
| 本体 | M5Stack Tab5（board v3 で動作確認） |
| SoC | ESP32-P4、WiFi は SDIO 接続の ESP32-C6 |
| メモリ | Flash 16 MB、PSRAM 32 MB |
| 画面 | 1280 × 720 MIPI-DSI タッチスクリーン |
| オプション | Tab5 キーボード、USB キーボード、TF カード |

## 開発状況

実機 Tab5 で、表示、タッチ、キーボード、WiFi、SSH（Tailscale 経由を含む）、日本語入力、
USB ディスクモードを確認済みです。音楽再生と OTA インストールはまだ十分に検証できていません。
不具合報告を歓迎します。

既知の制限: ESP32-P4 と C6 間の SDIO の制約により、Web ファイルで大きなファイルや多数のファイルを
アップロードすると不安定になることがあります。大きなファイルの転送には USB ディスクモードを使ってください。

## 書き込み

[ESP-IDF v5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32p4/get-started/)（esp32p4 ターゲット）が必要です。

```sh
git clone https://github.com/Mozgi512/Clientre5.git
cd Clientre5/firmware
idf.py set-target esp32p4
idf.py build
(cd updater && idf.py set-target esp32p4 && idf.py build)
sh tools/merge_flash.sh        # -> release/full/clientre5_full_flash.bin
python -m esptool --chip esp32p4 -p PORT --baud 1500000 write_flash \
  --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0 release/full/clientre5_full_flash.bin
```

リリースが公開されている場合は、ビルドせずに [Releases](https://github.com/Mozgi512/Clientre5/releases)
の `clientre5_full_flash.bin` をアドレス `0x0` に書き込むこともできます。

パーティション構成（アプリ `0x20000`、アップデーター `0xF80000`）は
[bmorcelli/Launcher](https://github.com/bmorcelli/Launcher) と互換です。Launcher から起動した場合は、
アプリ内 OTA ではなく Launcher で更新してください。

ソース構成、パーティションごとの書き込み、フォント・辞書の生成、実装の詳細は
[firmware/README.md](firmware/README.md)（英語）を参照してください。

## ドキュメント

- [クイックスタート（日本語）](docs/quick-start.ja.md)
- [Quick start (English)](docs/quick-start.md)
- [ファームウェア詳細（英語）](firmware/README.md)

## リポジトリ構成

```text
├── firmware/            ESP-IDF プロジェクト
│   ├── main/            アプリ本体（UI、ターミナル、SSH、IME、メディア、ネットワーク、OTA）
│   ├── updater/         OTA で使う factory 領域のアップデーター
│   ├── components/      同梱の libssh、MicroLink、wireguard-lwip
│   └── tools/           フォント / アイコン / 辞書の生成、リリーススクリプト、UI プレビュー
├── cad/                 STEP モデル: 改造バッテリーケース、JIS キーボード（CC BY 4.0）
└── docs/                ユーザーガイドと UI 画像
```

## ライセンス

Clientre 5 独自のコード（`firmware/main`、`firmware/updater`、`firmware/tools`）は
[MIT License](LICENSE) で公開しています。同梱のサードパーティ製コンポーネントはそれぞれのライセンスに従います。

| コンポーネント | ライセンス |
|---|---|
| [libssh](https://www.libssh.org/)（ewpa/LibSSH-ESP32 経由） | LGPL-2.1 |
| [MicroLink](https://github.com/Csontikka/microlink) | MIT |
| [wireguard-lwip](https://github.com/smartalock/wireguard-lwip) | BSD-3-Clause |
| [LVGL](https://lvgl.io/) | MIT |
| ESP-IDF と Espressif コンポーネント | Apache-2.0 |
| SKK-JISYO.M（辞書データ） | GPL-2.0-or-later |
| x12y12pxMaruMinya「マルミーニャ」hicc 作（ビットマップ化した部分集合のみ。TTF は同梱しません） | 無料・商用利用可・加工/埋め込み可 |
| Noto Sans CJK、DejaVu Sans Mono（生成フォントデータ） | OFL-1.1 / Bitstream Vera |

このため、ファームウェアのバイナリには LGPL・GPL のコンポーネントが含まれます。完全なソースコードはこのリポジトリにあります。

[`cad/`](cad/README.md) の CAD データは [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.ja) で公開しています。
