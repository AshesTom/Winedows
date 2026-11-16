# Winedows（Windows XP、Wine 版）

[English](../../README.md) · 日本語

> [!WARNING]
> この版の開発は終了しました。今後は更新されません。
> ネイティブ版を使ってください: [`nativeshell`](https://github.com/AshesTom/Winedows/tree/nativeshell) ブランチ、[リリース](https://github.com/AshesTom/Winedows/releases)。

Windows XP の `explorer.exe` を、パッチを当てた Wine の上で Linux のデスクトップシェルとして動かします。
Linux のアプリにも XP のウィンドウ枠、タスクバーのボタン、通知領域のアイコンが付きます。

![デスクトップ](../../docs/images/screenshot.png)

| | |
|---|---|
| ![クラシック](../../docs/images/screenshot-classic.png) | ![ReactOS のシェル](../../docs/images/screenshot-reactos.png) |

## 機能

- XP のシェル: タスクバー、スタートメニュー、デスクトップ、フォルダウィンドウ（Luna またはクラシック）
- Linux のアプリをタスクバー、Alt+Tab、すべてのプログラムで扱えます
- Linux のトレイアイコンを XP の通知領域に表示（ネットワーク、音量、バッテリーのアイコン付き）
- ISO から取り出した XP のプログラムとコントロールパネル: メモ帳、ペイント、Internet Explorer 6、Windows Media Player 9、画面のプロパティ、ユーザー アカウント、ネットワーク接続
- Wine 11.0 による Windows アプリ（32 ビット、64 ビット）
- 英語版と日本語版の XP、fcitx5 と Mozc による日本語入力
- XP のようこそ画面（LightDM のグリーター、任意）
- XP の ISO が無いときの ReactOS のシェル
- セッション全体で約 520 MB のメモリ

## 必要なもの

- 新しくインストールした Debian 13
- Windows XP Professional SP3 x86 の ISO（英語版または日本語版）
- Wine のビルドに約 40 GB の空き容量（4 コアで約 90 分）

## インストール

```sh
git clone -b WinXP https://github.com/AshesTom/Winedows.git ~/winedows
cd ~/winedows
scripts/setup.sh --media /path/to/windows-xp.iso --greeter
```

インストール後に再起動してください。XP の ISO が無い場合は ReactOS のシェルを使います:

```sh
scripts/setup.sh --shell reactos --lang ja
```

オプションと設定: [INSTALL.md](../../INSTALL.md)

## 注意

実験的な非公式プロジェクトです。Microsoft や Wine プロジェクトとは関係ありません。Microsoft のファイルは配布していません。
Windows XP の ISO はご自身で用意してください。取り出したファイルは `localres/` と `.local/` に置かれます。

## ライセンス

LGPL-2.1-or-later（このリポジトリのコードとパッチのみ）。`wayland/patches/labwc/` は GPL-2.0-only、
`wayland/patches/wlroots/` は MIT です。Windows XP とそのファイルの著作権は Microsoft にあります。
[mikuta0407/win4wine](https://github.com/mikuta0407/win4wine) のフォークから始まりました。
