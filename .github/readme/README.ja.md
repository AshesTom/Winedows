# Winedows（Windows 7、Wine 版）

[English](../../README.md) · 日本語

> [!WARNING]
> この版の開発は終了しました。今後は更新されません。
> ネイティブ版を使ってください: [`nativeshell`](https://github.com/AshesTom/Winedows/tree/nativeshell) ブランチ、[リリース](https://github.com/AshesTom/Winedows/releases)。

Windows 7 SP1（x64）の `explorer.exe` を、パッチを当てた Wine の上で Linux のデスクトップシェルとして動かします。
Linux のアプリにも Windows 7 のウィンドウ枠が付き、タスクバーにまとまります。

![デスクトップ](../../docs/images/screenshot-win7.png)

| | |
|---|---|
| ![ログオン画面](../../docs/images/screenshot-win7-logon.png) | ![小さいタスクバーのアイコン](../../docs/images/screenshot-win7-small.png) |

## 機能

- Windows 7 のシェル: スタートメニュー、タスクバー、通知領域、デスクトップ、フォルダーウィンドウ、ライブラリ
- Windows アプリでも Windows 7 のファイルのダイアログ
- Linux のアプリをタスクバー、Alt+Tab、スタートメニューで扱えます
- Aero Glass、タスクバーのライブサムネイル、Aero プレビュー（実験的）
- Windows 7 のログオン画面（LightDM のグリーター、任意）、Aero のカーソル、Segoe UI
- Wine 11.0 による Windows アプリ（32 ビット、64 ビット）
- 英語版と日本語版の Windows 7、fcitx5 と Mozc による日本語入力

## 既知の制限

- ネットワークと音量のアイコンは正しい状態を表示しません。
- ネットワークフォルダー、ヘルプとサポート、コントロールパネルの大部分はありません。
- ロック、スリープ、休止状態は動作しません。

## 必要なもの

- 新しくインストールした Debian 13
- Windows 7 SP1 x64 の ISO
- Wine のビルドに約 40 GB の空き容量（4 コアで約 90 分）

## インストール

```sh
git clone -b Win7 https://github.com/AshesTom/Winedows.git ~/winedows
cd ~/winedows
scripts/setup.sh --media /path/to/windows-7.iso --greeter
```

インストール後に再起動してください。オプションと設定: [docs/windows7.md](../../docs/windows7.md)

## 注意

実験的な非公式プロジェクトです。Microsoft や Wine プロジェクトとは関係ありません。Microsoft のファイルは配布していません。
Windows 7 の ISO はご自身で用意してください。取り出したファイルは `localres/` と `.local/` に置かれます。

## ライセンス

LGPL-2.1-or-later（このリポジトリのコードとパッチのみ）。`wayland/patches/labwc/` は GPL-2.0-only、
`wayland/patches/wlroots/` は MIT です。Windows 7 とそのファイルの著作権は Microsoft にあります。
[mikuta0407/win4wine](https://github.com/mikuta0407/win4wine) のフォークから始まりました。
